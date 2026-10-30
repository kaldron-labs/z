# FIX measured baseline

This report describes the captured 2026-10-28 build, before the repairs in
[the 2026-10-29 implementation audit](../AUDIT.md). Its archive and numbers are
unchanged; they do not measure the audited implementation.

Raw evidence: [dated archive](results/2026-10-28.tar.xz); [unpacking and reproduction](results/README.md). Paths below refer to the unpacked archive.

This is an optimized **instrumented** baseline. Zfix was compiled by Clang 21.1.8
with `-O3 -DNDEBUG`, retaining `-DZDEBUG` and ASan to match the unchanged existing
Clang 21 dependency DSOs (`-O1 -g -DZDEBUG`, ASan). The pinned QuickFIX SDK uses
Clang 21, `-O2 -g -DNDEBUG`, GNU C++17 and ASan. System crypto and other external
libraries are uninstrumented. These results do not establish ordinary release
or production throughput. No dependency was rebuilt for this baseline.

All 144 timing cases and 144 separate allocation/copy cases passed. Each case
uses one session, 200 warmup orders, 2,000 measured orders, 64 in flight and seed 1;
all four pairings, TCP/TLS and six profiles have three repetitions. Group loads
use 32 parties with eight subparties each; binary data is 4,096 bytes. Relay
fragmentation uses 64-byte writes. Ordinary profiles saturate the bounded client; overload
schedules at 1,000,000 orders/s. Both peers use 4,096 retained messages, 64 MiB history
and 16 MiB queue bounds. The archive preserves exact arguments, counts, resources,
heap observations, diagnostic counters and stderr. Native history retains
replayable bodies; QuickFIX retains serialized frames. Matching limits cover
different stored representations. No QA or compilation ran
concurrently with measurement; the host is shared and not a controlled lab.

Process quantiles have 2,000 samples per run, so p99.9 has only about two tail
observations. Codec quantiles have 10,000 samples per shape/run (1,000 warmup),
about ten at that tail. The standalone group shape uses eight parties,
four subparties each and 32 text bytes. Repetitions are reported separately
or summarized as specified below, without claiming statistical confidence bounds. Latencies
include queueing at the declared in-flight window. CPU/RSS include startup,
warmup and shutdown; fork/exec RSS can include the inherited parent high-water
floor. Retained-byte peaks describe application history, not total process heap.

Allocation diagnostics are separate runs. Framework allocation/live-byte
snapshots cover framework heaps at logical drain, excluding SDK/STL/libc.
LD_AUDIT counts selected main-namespace PLT allocation attempts and copy entries:
free includes null pointers, requested bytes include full realloc requests,
ASan copy counts are a subset, and nested calls can be counted. Direct/inlined
and custom allocator internal calls are excluded. No diagnostic counts are
combined with timing distributions.

Verification is scoped to Linux x64, exercised with GCC 15.2 and Clang 21.1.8.
ARM and Windows are outside this task's scope.

This run measures the refactored `Link` build with a shared codec-fixture time,
`20261028-12:34:56.123456789`. Both engines encode the same timestamp and produce
170/910/4281-byte scalar/group/binary frames. The earlier wall-clock fixture
could expose different trailing-zero precision; its rejected run is retained
under `prior/pre-codec-input-fix/`, including the failed verification. The
original pre-Link snapshot is separate under `prior/pre-Link/`. These tables
use only the fresh corrected run; earlier samples are neither substituted nor
merged.

Current retained-data checks are in `process/verification.json`; the pinned
post-Link correctness and installed-consumer evidence is under
`verification/current/`, indexed by `verification/current/index.json`.

Build label: `clang21-O3-ZDEBUG-ASAN-existing-Clang-ASAN-dependencies`. Platform: Linux-7.0.0-1014-aws-x86_64-with-glibc2.43.
CPU: Intel(R) Xeon(R) Platinum 8488C. Affinity: [0, 1, 2, 3, 4, 5, 6, 7].
Compiler: `clang++`. Flags: `-march=x86-64-v2 -g -DZDEBUG -O3 -DNDEBUG -fsanitize=address -fno-omit-frame-pointer`.

These are local process measurements on the recorded host. The table uses median per-session completion rate and median p50 across repeats; worst p99 is the largest individual-run p99. Quantiles are never added or merged. Full p50/p95/p99/p99.9/max, wire counts, resources, retained bytes, errors, diagnostic allocation/copy counts, exact commands and hashes are in the archived `process/report.md`, `process/manifest.json` and individual `case.json` files.

The regression envelope is 1.25 times the worst observed intended-terminal p99, rounded up. It is a measured baseline for equivalent runs on this host, not a production service-level promise or a portable CI threshold. Startup, TLS relay and shared-host effects are described in the archived `process/report.md`. Overload intended latency includes the delay before the bounded client can submit.

| Pair / transport / shape / session | Runs | Orders/s median | Ack p50 median ns | Ack p99 worst ns | Terminal p99 worst ns | Intended terminal p99 worst ns | Regression envelope ns |
| --- | --- | --- | --- | --- | --- | --- | --- |
| native-native / tcp / binary / 1 | 3 | 11370.3 | 5489735 | 6871878 | 6909120 | 6909120 | 8636400 |
| native-native / tcp / fragmentation / 1 | 3 | 2784.0 | 23047423 | 41165260 | 41195972 | 41195972 | 51494965 |
| native-native / tcp / groups / 1 | 3 | 4418.8 | 11412583 | 21445749 | 21467020 | 21467020 | 26833775 |
| native-native / tcp / overload / 1 | 3 | 27189.3 | 2295759 | 2755801 | 2768762 | 71365586 | 89206983 |
| native-native / tcp / replay / 1 | 3 | 19965.6 | 3144417 | 4152439 | 4157823 | 4157823 | 5197279 |
| native-native / tcp / scalar / 1 | 3 | 28589.6 | 2221584 | 2499569 | 2514318 | 2514318 | 3142898 |
| native-native / tls / binary / 1 | 3 | 10800.8 | 5886390 | 6988113 | 7019069 | 7019069 | 8773837 |
| native-native / tls / fragmentation / 1 | 3 | 1780.1 | 34725889 | 59208112 | 59227751 | 59227751 | 74034689 |
| native-native / tls / groups / 1 | 3 | 4790.9 | 10871284 | 22012284 | 22025637 | 22025637 | 27532047 |
| native-native / tls / overload / 1 | 3 | 17880.1 | 3549434 | 3999012 | 4026648 | 110930821 | 138663527 |
| native-native / tls / replay / 1 | 3 | 16280.6 | 3820297 | 5458363 | 5484619 | 5484619 | 6855774 |
| native-native / tls / scalar / 1 | 3 | 17721.2 | 3511724 | 4443015 | 4477813 | 4477813 | 5597267 |
| native-quickfix / tcp / binary / 1 | 3 | 7601.9 | 8353030 | 10013736 | 10038004 | 10038004 | 12547505 |
| native-quickfix / tcp / fragmentation / 1 | 3 | 2970.8 | 21547057 | 29831868 | 29870668 | 29870668 | 37338335 |
| native-quickfix / tcp / groups / 1 | 3 | 1725.8 | 36120203 | 50684271 | 50709374 | 50709374 | 63386718 |
| native-quickfix / tcp / overload / 1 | 3 | 18238.4 | 3251052 | 4718818 | 4752544 | 113162758 | 141453448 |
| native-quickfix / tcp / replay / 1 | 3 | 15406.3 | 4086781 | 5992216 | 6008656 | 6008656 | 7510820 |
| native-quickfix / tcp / scalar / 1 | 3 | 20717.6 | 3020938 | 4295904 | 4331730 | 4331730 | 5414663 |
| native-quickfix / tls / binary / 1 | 3 | 7167.2 | 9054057 | 11439549 | 11508431 | 11508431 | 14385539 |
| native-quickfix / tls / fragmentation / 1 | 3 | 2053.9 | 30880478 | 44689052 | 44721270 | 44721270 | 55901588 |
| native-quickfix / tls / groups / 1 | 3 | 1529.4 | 39857001 | 57475636 | 57516096 | 57516096 | 71895120 |
| native-quickfix / tls / overload / 1 | 3 | 14771.5 | 4266766 | 5551912 | 5584847 | 138086478 | 172608098 |
| native-quickfix / tls / replay / 1 | 3 | 15825.3 | 3965299 | 5935881 | 6004838 | 6004838 | 7506048 |
| native-quickfix / tls / scalar / 1 | 3 | 14704.2 | 4281701 | 4866511 | 4902724 | 4902724 | 6128405 |
| quickfix-native / tcp / binary / 1 | 3 | 7789.8 | 8323917 | 10185209 | 10267626 | 10267626 | 12834533 |
| quickfix-native / tcp / fragmentation / 1 | 3 | 2770.7 | 23217294 | 40791252 | 40875627 | 40875627 | 51094534 |
| quickfix-native / tcp / groups / 1 | 3 | 891.7 | 71539569 | 85601102 | 85603482 | 85603482 | 107004353 |
| quickfix-native / tcp / overload / 1 | 3 | 14964.8 | 3857605 | 5558982 | 5593620 | 149011498 | 186264373 |
| quickfix-native / tcp / replay / 1 | 3 | 14337.1 | 4232794 | 7103625 | 7166139 | 7166139 | 8957674 |
| quickfix-native / tcp / scalar / 1 | 3 | 12835.9 | 5035851 | 5650466 | 5701074 | 5701074 | 7126343 |
| quickfix-native / tls / binary / 1 | 3 | 6832.3 | 9090068 | 10882046 | 10934673 | 10934673 | 13668342 |
| quickfix-native / tls / fragmentation / 1 | 3 | 1767.6 | 36670935 | 50203956 | 50210474 | 50210474 | 62763093 |
| quickfix-native / tls / groups / 1 | 3 | 898.4 | 70924139 | 86800670 | 86803981 | 86803981 | 108504977 |
| quickfix-native / tls / overload / 1 | 3 | 10484.5 | 6116889 | 6819421 | 6869234 | 191464012 | 239330015 |
| quickfix-native / tls / replay / 1 | 3 | 11391.6 | 5400629 | 8557317 | 8620516 | 8620516 | 10775645 |
| quickfix-native / tls / scalar / 1 | 3 | 12274.8 | 4773024 | 7230140 | 7286848 | 7286848 | 9108560 |
| quickfix-quickfix / tcp / binary / 1 | 3 | 7484.4 | 8384284 | 9861680 | 9934129 | 9934129 | 12417662 |
| quickfix-quickfix / tcp / fragmentation / 1 | 3 | 2919.5 | 22092654 | 31439811 | 31480609 | 31480609 | 39350762 |
| quickfix-quickfix / tcp / groups / 1 | 3 | 922.1 | 69511057 | 81664720 | 81667700 | 81667700 | 102084625 |
| quickfix-quickfix / tcp / overload / 1 | 3 | 17192.5 | 3659302 | 5120927 | 5153721 | 128482755 | 160603444 |
| quickfix-quickfix / tcp / replay / 1 | 3 | 15560.1 | 3606601 | 7671866 | 7733894 | 7733894 | 9667368 |
| quickfix-quickfix / tcp / scalar / 1 | 3 | 17277.0 | 3614635 | 4907877 | 4934201 | 4934201 | 6167752 |
| quickfix-quickfix / tls / binary / 1 | 3 | 7363.2 | 8535889 | 10679994 | 10739844 | 10739844 | 13424805 |
| quickfix-quickfix / tls / fragmentation / 1 | 3 | 1939.5 | 32909953 | 55506346 | 55543268 | 55543268 | 69429085 |
| quickfix-quickfix / tls / groups / 1 | 3 | 879.2 | 73020916 | 85445335 | 85447828 | 85447828 | 106809785 |
| quickfix-quickfix / tls / overload / 1 | 3 | 13860.4 | 4513454 | 6492977 | 6544011 | 167762338 | 209702923 |
| quickfix-quickfix / tls / replay / 1 | 3 | 11993.7 | 4909106 | 9638381 | 9677661 | 9677661 | 12097077 |
| quickfix-quickfix / tls / scalar / 1 | 3 | 13353.7 | 4484110 | 7126910 | 7159786 | 7159786 | 8949733 |

## Standalone codec

Per-call encode includes typed body and full FIX framing; decode includes framing/checksum, typed construction and validation. QuickFIX also applies its canonical dictionary validation. Native reuses its output backing; QuickFIX constructs its SDK message and returns its SDK string. QuickFIX decode includes its input string and selected-field arena copies; Native decoded spans borrow input. These ownership costs are included. Clock-pair overhead is reported separately without subtraction.

| Engine / shape | Runs | Frame bytes | Encode p50 median ns | Encode p99 worst ns | Decode p50 median ns | Decode p99 worst ns | Clock p50 median ns |
| --- | --- | --- | --- | --- | --- | --- | --- |
| quickfix / binary | 3 | 4281 | 16073 | 43598 | 19724 | 45627 | 40 |
| quickfix / groups | 3 | 910 | 120521 | 160331 | 79023 | 106745 | 41 |
| quickfix / scalar | 3 | 170 | 11051 | 30407 | 14405 | 34598 | 40 |
| zfix / binary | 3 | 4281 | 5056 | 10358 | 2633 | 5250 | 40 |
| zfix / groups | 3 | 910 | 16911 | 31699 | 29992 | 58371 | 41 |
| zfix / scalar | 3 | 170 | 3936 | 5094 | 1728 | 2160 | 40 |

Raw artifacts: the `process/` subtree in the dated archive.

## Executable and library size

`.text` measures that ELF section; file bytes include debug/symbol data.
Shared libraries are listed separately and must not be attributed to only one
peer. All other loaded libraries, section sizes and hashes remain in the
manifests. These rows do not sum to a whole-process code footprint.

| Program | ELF file bytes | .text bytes |
| --- | --- | --- |
| native_client | 17295552 | 7138032 |
| native_codec | 3339504 | 1684512 |
| native_server | 17295240 | 7138032 |
| quickfix_client | 6101744 | 2428944 |
| quickfix_codec | 3127264 | 1501648 |
| quickfix_server | 6102520 | 2428944 |

| Framework/SDK library | ELF file bytes | .text bytes |
| --- | --- | --- |
| libZdbus-10.0.0.so | 2963992 | 740014 |
| libZe-10.0.0.so | 246760 | 51855 |
| libZf-10.0.0.so | 3249504 | 981553 |
| libZfix-10.0.0.so | 2701096 | 1073521 |
| libZfixFmt-10.0.0.so | 650232 | 199510 |
| libZi-10.0.0.so | 7767512 | 1921995 |
| libZm-10.0.0.so | 1734448 | 410546 |
| libZt-10.0.0.so | 865424 | 229369 |
| libZtcp-10.0.0.so | 49992 | 5232 |
| libZtls-10.0.0.so | 5865224 | 1794611 |
| libZu-10.0.0.so | 384512 | 97579 |
| libquickfix.so.17 | 15246632 | 1985394 |
