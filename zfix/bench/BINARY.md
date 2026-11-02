# Binary codec and retained-body measurements

The final clang release run completed 156 commands without failures or stderr.
Borrowed Binary decoding stayed close to constant time across the tested sizes.
Retained metadata moved the pool boundary and can cause an extra allocation and
copy when output grows. The prior ordinary sample is uncontrolled, so these
results do not establish a causal performance improvement or regression.

Raw JSON, observer reports, the runner, configuration and source/ELF hashes are
in [2026-11-01-binary.tar.xz](results/2026-11-01-binary.tar.xz).

## Reproduction and provenance

The run used Ubuntu clang 21.1.8, GNU C++2b,
`-march=x86-64-v2 -O3 -g -DNDEBUG`, and existing compatible prerequisites.
All benchmark processes were pinned to permitted CPU 0 on an Intel Xeon Platinum
8488C. Repository build/test jobs were idle. Frequency and other host activity
were not controlled. These are warm microbenchmarks, not production throughput
or tail-latency guarantees. Clock-pair overhead was 22 ns p50 and is not subtracted.

The archive's `benchmark-run.py` and `manifest.json` contain exact commands,
affinity and hashes. The sequence was:

- Three repetitions of `nativecodec 10000 1000`.
- Three repetitions of `binarycodec 10000 1000 SIZE` at 0, 32, 4096 and 65536
  payload bytes, covering six text/Binary String/Bytes live/indexed modes.
- Dense/sparse `txbodycodec 10000 1000 SIZE` scans, actual capacity transition
  discovery, and three repetitions at each adjacent boundary.
- Separate heaptrace processes varying count, size and encode/decode phase.
  Additional boundary diagnostics use warmup 10000 so all extra measured IDs have
  five decimal digits; warmup 1000 crossed an ID-width boundary and is retained
  as an explicitly confounded observation.

The observer was compiled in `zfix/bench` with:

```sh
clang++ -std=gnu++2b -march=x86-64-v2 -O3 -g -DNDEBUG \
  -DZ_VMAJOR=10 -DZ_VMINOR=0 -DZ_VPATCH=0 \
  -fPIC -shared -fno-builtin -Wall -Wextra -isystem ../../zu/src \
  heaptrace.cc -ldl -o heaptrace.so
```

Its SHA-256 is
`c05a6fec08125cfabafa73bcf7bc135db4df4819413a71caa365a02467ea32fc`.
Public interposition symbols remain visible. Timings precede instrumentation;
observer timings are not used below.

## Ordinary complete frames

Values are median of three run p50s, with their minimum–maximum in parentheses,
in nanoseconds. The historical observation used the same count/warmup and frame
extents, but a different collection time without controlled affinity or repeated
runs. Its raw file is preserved as `prior-ordinary.jsonl` in the archive.

| Shape / operation | Prior single p50 | Current p50 median (range) |
| --- | ---: | ---: |
| scalar encode | 1252 | 743 (738–743) |
| scalar decode | 714 | 326 (324–328) |
| groups encode | 5607 | 3729 (3690–3746) |
| groups decode | 5004 | 2694 (2684–2700) |

Scalar frames remain 170 bytes and group frames 910 bytes. Current p50s were
lower than that prior observation; the different collection conditions prevent
attributing the difference to this change. Exact frame tests and logical codec
checks are functional evidence separate from this comparison.

## Body-only payload measurements

Entries are median p50 encode / decode nanoseconds across three runs. Full
quantiles and individual repetitions are preserved in the archive. String and
Bytes denote Binary mappings. Ordinary text contains letters without SOH;
Binary data contains hostile octets and FIX-looking fields, so cross-class
ratios combine content and framing differences.

| Payload bytes | Text live | Text indexed | String live | String indexed | Bytes live | Bytes indexed |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 90 / 67 | 92 / 69 | 127 / 73 | 123 / 76 | 121 / 74 | 126 / 75 |
| 32 | 94 / 67 | 95 / 70 | 127 / 72 | 133 / 77 | 120 / 72 | 125 / 76 |
| 4096 | 101 / 95 | 101 / 98 | 133 / 74 | 133 / 77 | 128 / 74 | 135 / 78 |
| 65536 | 1241 / 753 | 1240 / 747 | 1474 / 79 | 1484 / 80 | 1476 / 79 | 1484 / 80 |

At 65536 bytes, Binary live decode run p50s were 79–80 ns for both representations;
indexed values were 80 ns. These bounded borrowed paths skip the advertised
payload rather than scan it for SOH. Encoding includes the necessary output
copy and grows with payload size. Pointer identity, full initial byte equality,
extent, ID and trailing-field checks ran alongside these workloads.

## Retained-body capacity and factory cost

Dense has three logical leaves; sparse adds twelve omitted leaves. At the same
payload and iteration their encoded bodies are identical. Actual retained length
differs by 96 bytes. Usable pooled capacity is 1040 bytes in this build, rather
than the nominal `ZiIOBuf_DefltSize` alone.

| Schema | Payload bytes | Body bytes | Retained bytes | Capacity | Memory charge | Factory p50 median (range), ns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dense | 986 | 1015 | 1040 | 1040 | 1216 | 204 (203–206) |
| dense | 987 | 1016 | 1041 | 2048 | 5312 | 206 (204–206) |
| sparse | 890 | 919 | 1040 | 1040 | 1216 | 218 (217–219) |
| sparse | 891 | 920 | 1041 | 2048 | 5312 | 217 (214–222) |

The extra omitted leaves shift the maximum inline payload from 986 to 890 bytes.
Crossing either boundary changes reported memory charge from 1216 to 5312 bytes.
At the same 928-byte payload, the coarse scan recorded identical 957-byte bodies,
but dense remained inline and sparse used jumbo storage. The coarse scan has
one run per size and cannot isolate small CPU costs of omitted-field range
capture. Boundary timings cover factory encoding, not final retained publication.

## Allocation and copy observations

For each of six body modes at 32, 4096 and 65536 bytes, increasing decode-only
count from 1000 to 10000 with warmup 1000 added **zero observed allocation calls
and zero observed copy calls**. Whole-process requested bytes increased by
360000 in each pair; sample storage and sorting are included. Copy-byte deltas
were −6 to +4 bytes from reporting differences. Indexed position storage still
belongs to its implementation; zero libc-call growth does not prove absence of
custom allocator operations.

For Binary Bytes live encode minus decode-only observations, the increase in
copy bytes between 4096 and 65536-byte payloads was 675840015 bytes, compared
with `11000 * (65536 - 4096) = 675840000`. The corresponding 32-to-4096 increase
was 44704002 versus 44704000. This is consistent with one dynamically observed
source-to-output payload copy per encoding, with small reporting differences.
It cannot exclude inlined or otherwise unobserved copies.

The retained factory does make observed allocations under this heap configuration.
The following are whole-process deltas for 9000 additional iterations, with
warmup 10000 controlling ID digit width:

| Schema / payload | Added allocation calls | Added copy calls | Added copy bytes |
| --- | ---: | ---: | ---: |
| dense / 986 | 9000 | 108000 | 9000002 |
| dense / 987 | 18000 | 117000 | 18369002 |
| sparse / 890 | 9000 | 108000 | 8136002 |
| sparse / 891 | 18000 | 117000 | 17505002 |

Within capacity, twelve omitted leaves add no extra allocation per body in the
matched cases. A boundary crossing adds a second allocation and a preservation
copy. Adjacent-size copy deltas differ by `9000 * (1040 + 1)`: one extra payload
byte plus preservation of 1040 already-written bytes per body. This late growth
copies payload too. Consequently, a blanket claim that every retained save
performs only one payload copy would be incorrect. Preallocated body emission
and borrowed decoding are distinct paths.

The observer sees dynamically dispatched libc calls, excluding inlined copies
and custom pool internals. Totals include initialization, payload/sample storage,
sorting and JSON output. Count/size scaling supplies scoped evidence; source
inspection and borrowed pointer checks remain necessary.

## Scoped source audit

The source supports the following scoped statements:

| Path | Code evidence | Limit of the conclusion |
| --- | --- | --- |
| Live field decoding | `ReadFields::Index` stores one primary-tag entry and typed loader per logical leaf. `readScope` does one lookup, then the loader consumes the ordinary value or whole binary pair and returns consumed bytes. | Session observation and catalog selection are separate operations; this does not mean every complete receive performs only one lookup or pass overall. |
| Borrowed binary assignment | `loadRawSpan` returns the input address and element extent. Owning destinations construct from the raw payload span. | Pointer checks establish borrowed storage identity; owning destinations intentionally allocate/copy according to their container. |
| Binary serialization | `saveBinary` obtains the raw octet extent, prints its decimal count, then writes the payload directly through the bounded sink. `Sink::put` copies directly to destination storage. | The necessary source-to-output copy remains. Formatter scratch, frame headroom movement and buffer growth are distinct costs. No complete encoded payload temporary appears in this path. |
| Ordinary serialization | The Binary branch is compile-time selected; ordinary fields retain their scalar save path. | Range capture adds work when saving through the retained `TxBody::EncodeSink`; the plain body codec does not measure that path. |
| Retained factory | `SaveRange` captures emitted root ranges during the existing typed save; omissions record sentinels. Slots share the existing pooled body allocation. | Each logical root leaf adds metadata, including omitted leaves. Capacity crossings can require jumbo storage. Factory timings and charge measurements must quantify this cost. |
| Final retained frame emission | The entry-generated emitter walks logical metadata, substitutes session values, and copies coalesced archived byte runs. It does not retokenize ordinary body bytes. | This is an additional metadata walk. Declared signatures and selectors enable final-frame validation; selector inspection and archive restoration have their own validation passes. Factory-only timings do not characterize final publication latency. |

The observer sees dynamically dispatched libc allocation and copy calls. It does
not see inlined copies, custom pool internals or all loader/runtime operations.
Process totals include static initialization, payload/sample storage, quantile
sorting and JSON output. Count scaling and size scaling can reveal additional
work visible to the observer, but cannot prove absence of all allocation or
copying. Borrowed pointer-identity checks, bounded wire comparisons and source
inspection supply separate evidence; they are not replaced by observer totals.
