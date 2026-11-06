# Measurement evidence

The current [unchecked-conversion comparison](CONVERT.md) measures native Zu
conversion without Zfix field prevalidation, including all four pairings.

The earlier [direct-loader comparison](LOADERS.md) measures tag-to-loader function
pointers, including all four engine pairings over TCP/TLS with four sessions.

The earlier [signed static-tag comparison](TAGS.md) measures the startup-populated
`int32_t` tag map with all four engine pairings, four sessions, and TCP/TLS.

The earlier [one-pass receive comparison](ONEPASS.md) includes the static tag
dispatch, text-only codec, before/after measurements and release validation.
The thread-owner and acceptor comparisons below precede this refactor.

The [fresh thread-owner comparison](THREADS.md) reruns all four pairings with
separate owners, a shared acceptor, and shared owners on both native peers.
It covers TCP/TLS, one/four sessions and three repetitions per case.

The current uninstrumented release comparison and the investigation of
QuickFIX-to-zfixd performance are in [ACCEPTOR.md](ACCEPTOR.md). The ASan
measurements below describe the earlier build.

## 10K orders at one and four sessions

The [2026-10-31 archive](2026-10-31-10k-multisession.tar.xz) compares all four
client/server pairings with the core at commit `2881f4e2` and measurement-only
timestamp/reporting additions retained in `measurement.patch`. Each case uses
10,000 measured orders and 1,000 warmup orders **per session**, 64 orders in flight per session,
and three repetitions. One and four concurrent FIX sessions run over TCP and
verified TLS on loopback, with one client process and one server process per
case. Each order produces an acknowledgement and a terminal execution report.
The scalar order workload, history/queue bounds and validation settings are
identical across session counts. Four sessions therefore measure 40,000 orders
per case with up to 256 orders in flight overall.

All 48 timing cases passed: 120 session runs, 1.2 million measured orders,
zero reported errors, zero unfinished orders and successful peer exits.
Executable hashes were independently rechecked after measurement. Cases ran
sequentially, without concurrent builds or QA, on the recorded shared Linux x64
host with an Intel Xeon Platinum 8488C.

These are optimized **instrumented** measurements. Native peers use Clang 21
with `-O3`, ASan and the existing foundation DSOs. The pinned QuickFIX SDK uses
Clang 21, `-O2 -g -DNDEBUG` and ASan; `ASAN_OPTIONS=detect_leaks=1` applies to
all peer processes. Native validation uses its declared typed dictionary;
QuickFIX uses the canonical FIX 4.4 dictionary and default validation. Both
retain bounded history, using different native-body/serialized representations.
The measured rates describe these builds and workloads, not clean release or
production throughput.

Pair names are **client-server**, with `native` meaning `zfix`/`zfixd`. Rates
below are medians across three repetitions of shared-window throughput: all
measured completions divided by the time from the first session's measured
start to the last session's measured finish, on the same client process's
monotonic clock. Thus the four-session rate uses a common interval rather than
summing four independent rates. The independent sum is also retained in the
full report. Ack p99 is the worst individual-session p99 across
all repetitions, without pooling or averaging quantiles.

| Client-server pair | Transport | 1 session orders/s | 4 sessions orders/s | 4 sessions worst ack p99 ms |
| --- | --- | --- | --- | --- |
| native-native | TCP | 31150.9 | 31681.3 | 10.683 |
| native-native | TLS | 25282.4 | 31307.6 | 10.528 |
| native-quickfix | TCP | 18969.5 | 17863.4 | 25.887 |
| native-quickfix | TLS | 15027.5 | 15934.1 | 23.927 |
| quickfix-native | TCP | 12792.5 | 13415.7 | 28.070 |
| quickfix-native | TLS | 10671.7 | 12551.9 | 29.918 |
| quickfix-quickfix | TCP | 15917.7 | 15071.1 | 27.665 |
| quickfix-quickfix | TLS | 12009.2 | 14483.1 | 27.905 |

Zfix/Zfix has the highest measured rate in both transports and session counts.
Four sessions do not deliver proportional rate scaling in this configuration;
the larger total in-flight window accompanies higher per-session latency.
This experiment does not isolate the cause of scaling limits.

The archive contains `sessions-1/` and `sessions-4/` with exact commands,
per-case results, client/server JSON and stderr, executable/library hashes and
whole-process CPU/RSS observations. `comparison.md` includes the rate, latency,
resource and individual-session tables; `comparison.json` retains numeric
comparison rows. `results.py` is the reducer used after timing, separately
hashed with the other archived files. `measurement.patch` records the exact
measurement harness and timestamp changes from the base commit. `measurement.json`
records runtime sanitizer settings and SDK flags. `verification.json` records
the independent completion/count/quantile/exit/hash/window audit. Generated private
keys and binaries are excluded.

CPU and RSS include startup, warmup, leak checking and shutdown. RSS can inherit
the fork/exec parent's pre-exec high-water floor; these resource figures are
not isolated measured-phase costs. All session distributions remain separate.
The complete report also retains p50/p95/p99/p99.9/max for each session/run.

Recheck and regenerate after unpacking:

```sh
tar -xJf 2026-10-31-10k-multisession.tar.xz
cd 2026-10-31-10k-multisession
sha256sum -c SHA256SUMS
python3 results.py sessions-1 sessions-4 --output regenerated.md
```

The recorded absolute paths remain provenance; regeneration uses the supplied
relative artifact directories. To repeat measurement, use the session-count
loop in [the benchmark commands](../README.md#four-pairing-process-loads)
with current local dictionary/SDK paths and an accurately labelled build.

Archive SHA256: `c78578adffcb778f3db1666d1acab1c7f97c476c92e7cbc8820e3790fef265cc`.

## Earlier measurement archive

The original archive is [2026-10-28.tar.xz](2026-10-28.tar.xz). Its root report,
`build-manifest.json`, codec rows and `process/` tree describe the fresh post-Link
run with the shared codec input timestamp `20261028-12:34:56.123456789`.
All 288 process cases and 18 codec rows pass the strict retained-data audit.
The native and QuickFIX scalar/group/binary frames have matching lengths of
170/910/4281 bytes. The generated measurement report and raw files are retained
without rewriting them; the repository report adds context and code-size tables.

The dated archives retain process manifests, individual case results, peer JSON
and stderr, allocation observations, codec result rows and their executable
provenance. Public fixture certificates are included; generated private keys and
binaries are excluded. Original absolute paths and commands remain as provenance.

Read [the measured baseline](../RESULTS.md) before comparing results. The recorded
build uses optimized, instrumented Zfix with existing debug/ASan dependencies;
it does not establish ordinary release or production throughput. Verification
is scoped to Linux x64; ARM and Windows are outside this task.

Unpack an archive and run `sha256sum -c SHA256SUMS` inside its dated directory.
Its `process/manifest.json` records the four peers and their actual loaded
libraries. `build-manifest.json` additionally records both codec executables,
exact codec commands and sanitizer settings. Individual distributions and raw
resources remain separate by session, repeat and diagnostic mode. Diagnostics
are excluded from timing summaries.

The archive separates the three datasets:

- Root `process/` and codec files: the current corrected measurement run.
- `prior/pre-Link/`: the original historical snapshot and its original audit.
- `prior/pre-codec-input-fix/`: the rejected post-Link snapshot, including its
  failed verification. Separate wall-clock samples and different fractional
  precision produced unequal codec frame lengths in that run.

The common fixed timestamp repairs fixture input only. Every process and codec
case was rerun; earlier samples are neither substituted nor combined. The Link
source comparison alone was not treated as performance identity. See
`PROVENANCE.md` for these boundaries.

`process/verification.json` records the current raw-data audit, including all
155 source hashes, six measured executables and 36 loaded libraries.
`verification/current/index.json` pins separate correctness evidence under
`compilers/`, `runtime/`, `installed/`, `examples/`, `memory/`, `layout/` and
`measurement/`. For example, current installed results and header/library checks
are `verification/current/installed/summary.json` and
`verification/current/installed/link-install-verification.json`; the two-compiler
gate and targeted codec-input repair are recorded under
`verification/current/compilers/`. Earlier delivery checks remain under
`verification/pre-layout/`. These checks do not add timing samples.

The packaged `measurement-verifier.py` independently checks retained case,
resource, heap, diagnostic, codec and cross-manifest provenance records. After
unpacking, it can recheck the snapshot without requiring the original binaries:

```sh
python3 measurement-verifier.py . --expected-source-count 155 \
  --output ../verification-recheck.json
```

Historical mode reports unavailable or changed current paths separately. Use
`--require-current-hashes` only when the original executable, dependency and
source paths are available and should still match the measured snapshot.

To regenerate the combined report after unpacking, invoke `zfix/bench/results.py`
from the repository with the extracted `process` directory and all six extracted
codec JSONL files as `--codec` arguments. Original commands and configure site
are included; adjust checkout, dependency and output paths for another machine.
Preserve compiler flags, dependency ABI/instrumentation, dictionary pins,
workload and heap settings when making a comparison. Regression envelopes are
host-specific observations, not portable thresholds or service promises.

Published archive SHA256 (`2026-10-28.tar.xz`):
`bb066944cf075af408fed5210c7199182b306e42bf7152adfb23092e9b4a8587`.
