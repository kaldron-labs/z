# Constructor metadata release rerun — 2026-10-09

Incremental `make -C zi/interop -j4`; retained Clang 23 release configuration,
`-march=x86-64-v2 -O3 -g -DNDEBUG`. Canonical reflection uses the same
release-built FlatBuffers 25.12.19 object as the preceding acceptance batch.
Runs pinned to CPU 6; current clock source HPET. Pool override and preload unset.
Each operation comparison has three fresh processes, 11 alternating-order
rounds per process, 1,000,000 operations per round and a 64-byte payload.
Save comparisons have one process each with the same round/count settings.
All embedded interoperability/construction prechecks passed. No separate test
suite or dependent rebuild was run. Times are sample means, not message tails.

## Load comparison

Per-process medians (nanoseconds per operation):

| Workload / process | Canonical | Native | Native / canonical |
| --- | ---: | ---: | ---: |
| ops / 1 | 2.042 | 2.015 | 0.987 |
| ops / 2 | 2.005 | 1.974 | 0.984 |
| ops / 3 | 2.021 | 1.980 | 0.979 |
| wideops / 1 | 72.115 | 62.103 | 0.861 |
| wideops / 2 | 71.663 | 61.605 | 0.860 |
| wideops / 3 | 72.507 | 61.775 | 0.852 |

## All operation medians

Medians across all 33 samples per codec; nanoseconds per operation.

| Workload | Operation | Canonical | Native |
| --- | --- | ---: | ---: |
| ops | read | 0.814 | 0.819 |
| ops | verify | 6.710 | 6.023 |
| ops | load | 2.034 | 1.993 |
| ops | reflect | 3.834 | 1.531 |
| ops | verify_reflected | 19.715 | 20.197 |
| ops | print | 75.487 | 75.112 |
| ops | verify_canonical_schema | 19.624 | 19.059 |
| ops | verify_native_schema | 20.102 | 20.192 |
| wideops | read | 0.816 | 0.815 |
| wideops | verify | 15.491 | 12.169 |
| wideops | load | 72.129 | 61.887 |
| wideops | reflect | 3.825 | 1.526 |
| wideops | verify_reflected | 49.978 | 45.316 |
| wideops | print | 338.770 | 337.339 |
| wideops | verify_canonical_schema | 49.661 | 42.729 |
| wideops | verify_native_schema | 51.217 | 45.316 |

## Save medians

Nanoseconds per operation; 11 samples per codec/mode.

| Workload | Mode | Canonical | Native |
| --- | --- | ---: | ---: |
| save | reuse | 74.428 | 56.918 |
| save | fresh | 114.489 | 106.742 |
| wide | reuse | 99.553 | 82.374 |
| wide | fresh | 154.645 | 142.372 |

The earlier 2.068 / 2.021 ns native/canonical narrow-load result used missing
`Ctor<N>` metadata. These corrected runs show parity or a small native advantage;
wide loads improve consistently. This supports the fixture diagnosis and gives
no reason to add a codec fast path for the former narrow-load difference.
Cross-run absolute changes also include host variation; use paired comparisons.

Raw samples, link commands and source/binary hashes accompany this report.
The benchmark source snapshot is `zifbiobench.cc`; historical acceptance
archives remain unchanged. These focused runs do not repeat the full matrix.
