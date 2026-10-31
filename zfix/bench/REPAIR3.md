# Dispatch simplification verification

The dispatcher now looks up the framed MsgType, scans one unique discriminator
when needed, and uses the ordinary selected-type decoder. Candidate schema
merging, recursive position projection, the same-schema shortcut, and codec
selector-capture hooks have been removed. Discriminator inspection stores only
an offset and length and allocates no position tree.

The 8/9/35 prefix remains enforced by framing. Catalog metadata rejects a
discriminator declared in an immediate or nested repeating group. Different
candidate objects and facets, unmodeled root selectors, header-prefix extraction,
trace metadata, and exact-byte Tx restore remain supported. Binary discriminator
inspection is outside this repair's contract; standalone binary codec and opaque
transport coverage remain intact. The process group fixture now uses text data.

## Measurements

`dispatchcodec` uses fixed ExecutionReport frames of 168 bytes without parties
and 320 bytes with eight parties. It compares primary dispatch, secondary
dispatch to one shared object/facet, and secondary dispatch to different schemas.
The selected execution object and callback are the same in each case. Each
process warms up 2,000 iterations and measures 20,000, with three repetitions
before and after. Encoding and framing are outside the timing interval. Clock
overhead is recorded separately without subtraction.

The configured Linux x86-64 Clang 21 build uses `-O3`, debug information and
ASan. This is an instrumented working-tree comparison, not a clean release
baseline. Exact flags, CPU, commands and source hashes are in the
[manifest](results/repair3/manifest.json); individual observations are alongside it.

Each table entry is the median of the three process-level quantiles, rather
than a quantile of pooled samples. Positive change means higher latency.

| Selection | Parties | Before p50 (ns) | After p50 (ns) | Change | Before p99 (ns) | After p99 (ns) |
|---|---:|---:|---:|---:|---:|---:|
| primary | 0 | 1856 | 1885 | +1.6% | 2476 | 2336 |
| same-schema | 0 | 1880 | 2155 | +14.6% | 2297 | 2605 |
| different-schema | 0 | 2402 | 2178 | -9.3% | 2880 | 2519 |
| primary | 8 | 5639 | 5759 | +2.1% | 21498 | 21690 |
| same-schema | 8 | 5701 | 6287 | +10.3% | 21620 | 22477 |
| different-schema | 8 | 6639 | 6252 | -5.8% | 22540 | 22773 |

The second scan increases shared-schema secondary latency in this fixture.
Heterogeneous schemas avoid the old projection work. Primary-path variation
provides context for run-to-run noise; these observations do not establish
production latency or a universal speedup. Group results include ordinary group
allocation and ASan instrumentation; their tails do not isolate selector cost.

## Allocation observation

The separate [inspection fixture](results/repair3/inspect.cc) repeats just the
field inspector, without allocating sample arrays. The existing Linux/glibc
`LD_AUDIT` observer recorded zero allocation attempts and zero copied bytes for
both [one inspection](results/repair3/inspect-1.json) and
[100,000 inspections](results/repair3/inspect-100000.json). This observer counts
selected PLT calls and excludes direct/inlined/custom allocator internals; the
inspector's source also contains no allocation or position-container operations.
These observations do not assert that selected object/group decoding allocates
nothing. Both old and new decoders may allocate ordinary group storage; only
the old heterogeneous path additionally retained a projected position tree.

## Verification and limits

- Module build: `make -C zfix -j8` passed without warnings, covering all
  source, test, utility, integration, example, interop and benchmark targets.
- Unit suite: 29 binaries, 351 TAP scopes, ASan/LSan enabled, passed. Schema
  fixtures include the positive case and 21 expected failures, including group
  discriminators, plus typed send/reply membership checks.
- Integration suite: TCP six binaries/127 scopes; TLS six/132; demo one/two;
  native peer processes one/one. All passed with ASan/LSan enabled, including
  ingress, sessions, recovery/replay, restore and text/group process workloads.
- Auxiliary GCC debug Valgrind dispatch, codec, group and frame consumers:
  zero errors and zero definite/indirect leaks. These compile current codec
  sources and reuse existing unsanitized staged foundation DSOs. They are not
  verification of a clean rebuild of every dependency.
- Top-level `make -j8` completed the Zfix targets and then failed in unrelated
  `zhttp/src/ZhttpTLS.cc`: `ztls/src/Ztls.hh` accesses a private `m_closing`
  member and treats a `bool` as the expected atomic interface. Neither module's
  working-tree sources were changed by this repair.
- No Windows or ARM execution, clean release comparison, installation rerun,
  or new historical process-load matrix is claimed.

Detailed build and test logs for this execution are in `/tmp/zfix-repair3`.
