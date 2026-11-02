# One-pass receive decoding

This records the earlier literal-case lookup snapshot. The subsequent signed
static-hash implementation is described in [TAGS.md](TAGS.md). The archived
measurements below apply to that earlier source snapshot.

Known numeric tag IDs now dispatch through schema-generated literal cases.
MsgType and secondary selector matching also use compile-time wire strings.
There are no `ZmLHash` users in `zfix/src`, no runtime tag table, and no
one-time table initialization. Unknown ordinary fields are skipped.

Ordinary writable objects are decoded in wire order: scan the tag/value,
dispatch to its typed field, assign it, advance. Nested groups have separate
presence bitmaps and delimiter state. Header fields are assigned directly;
managed Rx reuses the decoded prefix when consuming the body. Validation-only
reads use the same scanner without constructing objects or calling getters.
Shared-object/facet secondary candidates decode once while retaining only the
selector span. Different representations inspect their selector before decoding.

Constructor-bound or non-writable component representations retain the scoped
positional fallback. Explicit raw-field inspection uses that fallback as well.
The ordinary receive path does not build a tag/value map or position tree.

Binary Length/Data support is removed: `FIX::Len`, raw-byte formatting,
`scanData`, binary sink mode, SecureData/Signature members, trailer emission,
length-pair tracing, workload encoded members and `--binary`/binary profiles.
Large-payload and fragmentation coverage uses ordinary Text(58). Explicit
Base64/Hex-style text encodings remain SOH-delimited fields. Existing evidence
archives describe their historical builds and are unchanged.

## Release validation

- Zfix rebuilt with `-j8`, Clang, `-march=x86-64-v2 -O3 -g -DNDEBUG`, no ASan.
  No dependency rebuilds or modules after Zfix.
- 29 unit binaries / 353 scopes passed, including wire-order setter execution
  before a later malformed tag, sparse known IDs and skipped unknown IDs.
- TCP/TLS integration passed 262 TAP scopes; independent codec/load/SDK checks
  and all 76 canonical cross-engine process cases passed.
- Text-only fuzz smoke passed 2,472 deterministic prefix/mutation cases,
  without sanitizer instrumentation.
- Compiler probes show generated comparisons/jump tables, with no hash or
  initialization-guard references. Loaded peers/libraries were checked for
  ASan/LSan references and active source/executable/library hashes rechecked.

## Codec comparison

Three separate processes per engine/build, 20K measured messages and 2K warmup
per shape. Values are medians of run p50 values, in nanoseconds. Identical
scalar/group frames are 170/910 bytes; input uses the same fixed timestamp.
Clock overhead is included, never subtracted from quantiles.

| Shape | Native decode before | Native decode final | QuickFIX decode final | Native encode before | Native encode final |
| --- | ---: | ---: | ---: | ---: | ---: |
| scalar | 442 | 449 | 3,366 | 827 | 782 |
| groups | 3,401 | 2,814 | 12,769 | 4,049 | 3,659 |

Scalar decode is essentially unchanged; group decode improves about 17%.
The final native decoder is about 7.5x faster than QuickFIX for the scalar
fixture and 4.5x for groups. Native typed validation/borrowed spans and SDK
canonical validation/owned copies retain their different implementation costs.
This is the complete refactor, including removal of unused binary handling,
not an isolated measurement of a single dispatch instruction.

## Four-session process comparison

All four pairings were rerun over TCP/TLS, three repetitions: 10K measured
orders and 1K warmup per session, 64 in flight/session. Both native peers
retain network Rx/Tx 1/2 and application Rx/Tx 3/4. Peers run in separate
loopback processes. Each snapshot passed 24 cases and 960K measured orders.
Rates are median shared-window orders/s; p99 is the worst session/repetition.

| Client-server | Transport | Before orders/s | Final orders/s | Before Ack p99 ms | Final Ack p99 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| native-native | TCP | 84,246 | 83,362 | 3.776 | 3.734 |
| native-native | TLS | 76,893 | 76,393 | 3.913 | 4.222 |
| native-quickfix | TCP | 48,918 | 47,908 | 6.234 | 6.142 |
| native-quickfix | TLS | 43,027 | 47,055 | 6.671 | 6.758 |
| quickfix-native | TCP | 54,567 | 60,738 | 5.663 | 5.566 |
| quickfix-native | TLS | 44,956 | 42,509 | 6.834 | 7.015 |
| quickfix-quickfix | TCP | 64,413 | 58,570 | 5.332 | 5.451 |
| quickfix-quickfix | TLS | 51,568 | 49,968 | 6.843 | 6.905 |

Sequential snapshots on the shared Xeon Platinum 8488C host are not an
interleaved causal experiment. The SDK library and foundation libraries are
unchanged; both fixture families were rebuilt against the smaller shared
workload model. QuickFIX-to-QuickFIX control throughput also varies. These
process measurements do not establish a universal end-to-end speedup or
tail-latency improvement. Per-order codec advantages do not eliminate the
transport, queueing, scheduling and opposite peer costs.

## Evidence

[Raw evidence](2026-11-01-onepass.tar.xz) includes exact before/after source
snapshots, manifests and program/library hashes, codec JSON, all process
cases, compiler assembly, final build/test logs and fuzz driver/results.
The baseline-only binary codec row is retained as historical raw output;
there is no final binary profile. Test private keys and executable backups
are excluded.

Archive SHA-256: `20f01c98033cf11db46668a83118425b70c4fb7b5c0f0f96b9e586fdfa29e0d5`.
