# Native conversion without field prevalidation

Zfix field conversion follows Postel’s law: native Zu conversion results are
delivered to the application without Zfix lexical, overflow, range, finite-value,
calendar, character or encoded-alphabet/padding prevalidation. Applications own
value admissibility. Framing/group structure, configured storage limits and
explicit `Required` presence remain enforced. Custom application converters
can still return their own errors.

Changes from the [direct-loader snapshot](LOADERS.md):

- Delete the checked integer helper, width/digit counting and lexical bound comparisons.
- Integers and floats call typed `Zu_nscan<Fmt>` directly; consumed-length and
  finite-value rejection are removed. The tag scanner uses the unchecked Zu
  entry point after establishing that input is available, with signed `int32_t` IDs.
- Default decimal/fixed fields use native `ZuDecimal`/`ZuFixed` scanning directly.
  Formatted decimal whole/fraction scans consume disjoint spans without a point-search pass.
- Timestamps use native scanners directly, without a lexical prepass, calendar
  round trip or TLS lookup for the receive formatter. Unix values use native decimal conversion.
- Range properties are ignored on load; library-owned Range declarations are removed.
- Strings and C strings copy/borrow supplied bytes without character/NUL validation.
  Encoded bytes go directly to the native decoder. Multiple-value separators
  are permissive; mapped enum/flag results are passed through.
- Skipped/read-only scalars and indexed raw/check operations no longer convert
  values merely to validate them. Indexed fallback prevalidation walks are deleted.
- Remove encoder character/finite-value rejection and header sequence/timestamp-order
  checks. Reversed original/sending timestamps are serialized as supplied.

The startup-populated `ZmLHashKV<int32_t, Loader>` table still calls stateless
field loaders directly. Repeated groups keep independent scope state; no
per-message tag/value map is built. Binary Length/Data support remains absent.

## Validation

- Clang 21 release, `-march=x86-64-v2 -O3 -g -DNDEBUG`, no ASan; only `zfix` rebuilt with `-j8`.
- 29 unit binaries / 355 TAP scopes pass. Obsolete rejection fixtures now assert
  pass-through: ignored ranges, wrapping/partial/native-null numeric results,
  raw text/C-string bytes, skipped fields, and timestamp-order preservation.
- TCP/TLS, demo and native process integration: 262 TAP scopes pass.
- Independent QuickFIX codec, load and SDK dependency checks pass.
- Assembly audit confirms direct loader calls and no checked-integer, lexical
  prepass or FIX receive-scan TLS machinery in the representative order decoder.
- 24 process cases pass: 960,000 measured orders, zero reported errors, unfinished
  orders, bad exits or timeouts. Source and 41 ELF hashes were checked; symbol
  inspection confirms no ASan/LSan instrumentation. Dependency DSOs, dictionary,
  flags and host/affinity match the preceding snapshot.

## Standalone codec

Three repetitions of 20,000 measured calls plus 2,000 warmup per shape. Values
are medians of run p50 values in nanoseconds; clock overhead is not subtracted.
Scalar/group wires remain 170/910 bytes. The fixture alternates full-frame encode
and decode. QuickFIX retains canonical dictionary validation; native intentionally
uses the requested permissive conversion policy. All measured messages are valid.

| Engine / shape | Before encode | After encode | Before decode | After decode |
| --- | ---: | ---: | ---: | ---: |
| zfix / scalar | 849 | 728 | 458 | 318 |
| zfix / groups | 4106 | 3627 | 3055 | 2668 |
| QuickFIX / scalar | 1565 | 1564 | 3375 | 3368 |
| QuickFIX / groups | 13431 | 13447 | 12699 | 12776 |

Native decode p50 falls about 31% scalar and 13% groups. Encode p50 falls
about 14% scalar and 12% groups. QuickFIX controls remain close. These are
sequential shared-host snapshots, not portable performance guarantees.

## Four-session process comparison

Each pairing/transport ran three times with four concurrent sessions, 10,000
measured orders plus 1,000 warmup per session and 64 orders in flight per session.
Both peers run as separate loopback processes on the recorded Xeon 8488C host.
Native peers retain separate multiplexer/application Rx/Tx owners. No builds or
QA ran concurrently with measurement. Rates are shared-window medians; worst
Ack p99 is the largest individual session p99, without merging quantiles.

| Client → server | TCP orders/s | TLS orders/s | TCP worst Ack p99 ms | TLS worst Ack p99 ms |
| --- | ---: | ---: | ---: | ---: |
| zfix → zfixd | 82,618 | 74,982 | 3.827 | 3.926 |
| zfix → QuickFIX | 47,819 | 44,483 | 6.215 | 6.791 |
| QuickFIX → zfixd | 55,290 | 45,001 | 5.743 | 6.984 |
| QuickFIX → QuickFIX | 61,566 | 47,934 | 6.007 | 6.643 |

Process throughput remains close despite lower codec costs. QuickFIX → zfixd
is still below QuickFIX → QuickFIX; this run does not establish that the earlier
acceptor performance gap is resolved.

## Evidence

The [archive](2026-11-01-convert.tar.xz) contains exact sources, commands/flags,
raw codec and per-session results, hashes, validation logs, optimized assembly
and audit code. The ephemeral TLS private key is excluded. Earlier archives
remain unchanged.
Archive SHA-256: `5d1ce6e875d84f10f39f7c47f47ca5fb3a58981792a09d8aa99864e7525a31f0`.
