# Direct loader function pointers

This records the direct-loader snapshot before [field prevalidation was removed](CONVERT.md).
The archive and measurements below apply to that earlier source snapshot.

The streaming map is `ZmLHashKV<int32_t, Loader>`, with
`Loader = unsigned (*)(ReadFields &, Token &)`. Startup initialization adds a
stateless lambda for each compile-time field ID. A successful lookup calls
its typed converter/setter directly, with no field-ordinal switch. No dispatch
heap allocation, captured callback state, lock or lazy-initialization guard is
introduced. Capacity remains schema-sized at no more than 25% occupancy.

The loader table is specialized by object, facet, filter and assignment policy.
Recovery and selector variants share that table. Per-scope context and presence
bits remain on the stack; groups retain independent scope state and unknown
fields are skipped in the one-pass scan. The separate ordinal metadata map
continues to serve scope membership, tracing and explicit indexed inspection;
constructor-bound/non-writable representations retain the indexed fallback.

## Validation

- Clang 21 optimized release, `-O3 -DNDEBUG`, no ASan; `-j8`, only `zfix` rebuilt.
- 29 unit binaries / 356 TAP scopes pass, including setter order, signed IDs,
  repeated groups, selection, header recovery and constructor-bound fallback.
- TCP/TLS, demo and native process integration: 262 TAP scopes pass.
- Independent QuickFIX codec, load and SDK dependency checks pass.
- Optimized assembly confirms startup `.init_array` initialization and direct
  loader calls in root, party and sub-party streaming loops. The remaining
  switch on reserved envelope tag IDs is unrelated to field-loader selection.
- All 24 process timing cases pass: 960,000 measured orders, zero errors,
  unfinished orders, bad exits or timeouts. Exact source and 41 ELF hashes were
  checked; no ASan/LSan symbols. Dependency DSOs, build flags, dictionary and
  host/affinity match the earlier [ordinal-map snapshot](TAGS.md).

## Standalone codec

Three runs of 20,000 measured calls plus 2,000 warmup per shape on the recorded
Intel Xeon 8488C host; medians of run p50 values, in nanoseconds. Scalar/group
wire sizes remain 170/910 bytes. Full framing/checksum and typed validation are
included; the QuickFIX fixture additionally applies its canonical dictionary.
Encode and decode are measured alternately by the unchanged fixture.

| Engine / shape | Ordinal-map encode | Direct-loader encode | Ordinal-map decode | Direct-loader decode |
| --- | ---: | ---: | ---: | ---: |
| zfix / scalar | 837 | 849 | 482 | 458 |
| zfix / groups | 3928 | 4106 | 3124 | 3055 |
| QuickFIX / scalar | 1564 | 1565 | 3374 | 3375 |
| QuickFIX / groups | 13409 | 13431 | 12761 | 12699 |

Native decode p50 falls from 482 to 458 ns scalar (about 5%) and from 3,124 to
3,055 ns groups (about 2%). Native encode p50 rises in this run, despite no
encoder source changes. These are sequential shared-host snapshots with an
alternating encode/decode fixture; they do not isolate cache/layout effects or
establish an end-to-end speedup. Raw runs and QuickFIX controls are retained.

## Four-session process comparison

Four concurrent sessions, 10,000 measured orders plus 1,000 warmup per session,
64 orders in flight per session; three repetitions per pairing/transport.
Both peers are separate loopback processes. Native peers retain separate
multiplexer/application Rx/Tx owners. No builds or QA ran during timing.
Rates are shared-window medians; worst Ack p99 is the largest individual
session p99 across repetitions, without merging quantiles.

| Client → server | TCP orders/s | TLS orders/s | TCP worst Ack p99 ms | TLS worst Ack p99 ms |
| --- | ---: | ---: | ---: | ---: |
| zfix → zfixd | 83,502 | 74,912 | 3.962 | 3.946 |
| zfix → QuickFIX | 47,067 | 43,259 | 6.213 | 6.819 |
| QuickFIX → zfixd | 54,433 | 43,436 | 5.828 | 6.906 |
| QuickFIX → QuickFIX | 60,169 | 51,904 | 6.155 | 8.020 |

QuickFIX → zfixd remains slower than QuickFIX → QuickFIX. Process controls
also varied between snapshots; this run does not demonstrate an overall
process throughput improvement from direct loaders.

## Evidence

The [archive](2026-11-01-loaders.tar.xz) contains exact source snapshots, build
and validation logs, commands/flags, raw per-session and codec results, source
and ELF hashes, audit code and optimized before/after lookup assembly.
The ephemeral TLS private key is excluded. Earlier archives are unchanged.
Archive SHA-256: `e53a946ae40e136292d0116620ea78c0e163c16585911e4659e7ded54bf0aace`.
