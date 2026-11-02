# Signed static tag lookup

This records the ordinal-valued map before the [direct-loader change](LOADERS.md).
The archived source and measurements below apply to that earlier snapshot.

Tag IDs are explicitly `int32_t`, including negative IDs. Each schema owns a
`ZmLHashKV<int32_t, unsigned>` with `ZmLHashStatic` and `ZmLHashLocal` policies.
The table is populated during program initialization from compile-time field
IDs, with schema-sized storage at no more than 25% occupancy. Its value encodes
a field ordinal and group flag; `ZuSwitch` selects the typed converter/setter.
Optimized Clang assembly confirms `.init_array` initialization and no lazy-init
guard on lookup. There is no per-message table population, allocation or lock.

The streaming path remains one pass: scan tag/value, look up the current scope,
set the field, advance. Unknown tags are skipped. Repeated groups keep separate
scope/presence state. Constructor-bound representations retain the indexed
fallback. Binary Length/Data support remains removed.

## Validation

- Clang 21 release configured through `./z.config -L`, `-O3 -DNDEBUG`, no ASan.
- Rebuilt only `zfix`, using `-j8`; dependency directories were not rebuilt.
- 29 unit binaries, 356 TAP scopes; schema rejection checks pass.
- TCP/TLS, demos and native process integration: 262 TAP scopes pass.
- Independent QuickFIX codec, load and dependency checks pass.
- Signed coverage includes both limits, overflow, known/unknown tags, duplicates,
  repeated group delimiters, known/unknown selectors, Reject.RefTagID, output
  round trips and the indexed fallback.
- 24 process timing cases pass: 960,000 measured orders; zero errors, unfinished
  orders, bad exits or timeouts. Source and 39 linked ELF hashes were rechecked;
  symbol inspection confirms no ASan/LSan instrumentation.

## Process measurements

Four concurrent sessions, 10,000 measured orders plus 1,000 warmup per session,
64 in flight per session; three repetitions of every pairing over TCP and TLS.
Both peers run as separate loopback processes on the recorded Intel Xeon 8488C
host. Native peers retain separate multiplexer and application Rx/Tx owners.
No builds or QA ran concurrently with measurement. Shared-window rates are
medians across repetitions; worst Ack p99 is the maximum individual session p99,
without merging or averaging quantiles.

| Client → server | TCP orders/s | TLS orders/s | TCP worst Ack p99 ms | TLS worst Ack p99 ms |
| --- | ---: | ---: | ---: | ---: |
| zfix → zfix | 81,969 | 73,855 | 3.832 | 3.964 |
| zfix → quickfix | 49,464 | 45,203 | 6.250 | 6.592 |
| quickfix → zfix | 56,067 | 47,052 | 5.645 | 6.898 |
| quickfix → quickfix | 62,015 | 53,177 | 5.648 | 6.495 |

QuickFIX → zfixd remains below QuickFIX → QuickFIX in this run. These are
shared-host observations, not a universal end-to-end improvement claim.

## Standalone codec

Three repetitions of 20,000 measured encode/decode calls and 2,000 warmup calls.
Values are medians of each run’s p50, in nanoseconds. The QuickFIX fixture also
applies canonical dictionary validation. Scalar/group wires remain 170/910 bytes.

| Decoder | Scalar encode | Scalar decode | Groups encode | Groups decode |
| --- | ---: | ---: | ---: | ---: |
| zfix | 837 | 482 | 3928 | 3124 |
| QuickFIX | 1564 | 3374 | 13409 | 12761 |

The previous [literal-case snapshot](ONEPASS.md) measured native decode p50
449 ns scalar and 2,814 ns groups. This signed static-map version measured
482 ns and 3,124 ns, about 7% and 11% slower respectively. QuickFIX controls
remained close (3,366/12,769 ns previously; 3,374/12,761 ns here). These separate
snapshots include both the lookup change and signed-tag handling; they do not
isolate either cost. The static map is the requested implementation, not a
demonstrated speedup over literal cases on these small schemas.

## Evidence

The [archive](2026-11-01-static-tags.tar.xz) contains exact source snapshots,
commands/build flags, process/library/source hashes, raw per-session results,
codec runs, validation logs, optimized lookup assembly and audit code.
The ephemeral TLS private key is excluded. Earlier archives remain unchanged.
Archive SHA-256: `e1a6f7cfe1526a0589e64c310168eb60e4cdeb0eb768b5a72b2930b784ca1c2f`.
