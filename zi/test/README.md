# Zi unit tests

FlatBuffers coverage is grouped by codec, reflection, and I/O behavior:

- `ZiFBTest`: metadata, explicit wire fixtures, verification, buffer construction
  and growth, borrowed views, values, object loading, unions and stream framing.
  Nested loading checks facet propagation through values, inline structs,
  vectors, fixed arrays, and unions, plus allocation/null handling for raw pointers, `ZuPtr`,
  and `ZmRef`, with reference counts and destruction checks.
- `ZiFlatcTest`: span tokenization, schema resolution, emission, invalid
  defaults/names/IDs, and staged-pair replacement rollback with injected I/O
  failure. Full generation/compilation checks live in `../itest`.
- `ZiFBReflectTest`: reflection access/writing, recursive BFBS generation,
  union schemas, facet-specific BFBS/FBS and external-schema verification.

`ZiFBTest` and `ZiFBReflectTest` use the fixture metadata in `ZiFBTest.hh`.
`ZiFBIOTest` covers pooled ownership, growth, alignment, IP mapping, and
transmit-stream output, including admission and send failure. Canonical
FlatBuffers interoperability remains in `../interop`.

`make test` runs these programs alongside the other Zi unit tests. Rebuild
incrementally with `make -C zi/src -j4` followed by `make -C zi/test -j4`.

## Negative compilation checks

Compile `ZiFBTest.cc` with the configured compiler/include flags,
`-fsyntax-only` and exactly one of these `-D` selectors per invocation:

- `ZIFB_BAD_MIXED`: mixed implicit and explicit field IDs.
- `ZIFB_BAD_GAP`: gaps in explicit IDs.
- `ZIFB_BAD_UNION`: union field without its preceding discriminator ID.
- `ZIFB_BAD_NAME`: duplicate field names.
- `ZIFB_BAD_ALIGN`: non-power-of-two alignment.
- `ZIFB_BAD_TAG_NAME`: generated discriminator name collision.
- `ZIFB_BAD_ALT_NAME`: reserved union alternative name.

Each invocation must fail on its intended metadata assertion. These branches
are excluded from the ordinary runtime program.

The `ZiFBIOTest` growth observer forwards to the existing `IOStorage` policy
and reports backing allocations, relocated scratch/output bytes, in-place tail
movement and detach compaction separately. Its paired transmit checks compare
actual bytes and final owner layout for a fitting grown object, and completed-span
insertion plus emitted-buffer allocations for the splitting fallback. Test
reference/observer copies are excluded from those counters. These checks
measure resource behavior separately from latency benchmarks.
