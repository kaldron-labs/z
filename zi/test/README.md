# Zi unit tests

FlatBuffers coverage is grouped by codec, reflection, and I/O behavior:

- `ZiFBTest`: metadata, explicit wire fixtures, verification, buffer construction
  and growth, borrowed views, values, object loading, unions and stream framing.
- `ZiFBReflectTest`: reflection access/writing, recursive BFBS generation,
  union schemas, facet-specific BFBS/FBS and external-schema verification.

The first two use the fixture metadata in `ZiFBTest.hh`.
`ZiFBIOTest` covers pooled ownership, growth, alignment, IP mapping, and
transmit-stream output, including admission and send failure. Canonical
FlatBuffers interoperability remains in `../interop`.

`make test` runs these programs alongside the other Zi unit tests. After changing
build declarations, regenerate through the repository's `z.config` wrapper
before rebuilding incrementally with `make -C zi/test -j4`.

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
reference/observer copies are excluded from those counters. This source addition
awaits milestone20 compilation and execution; it is not a latency measurement.
