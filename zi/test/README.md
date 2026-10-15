# Zi unit tests

`ZpbTest` covers protobuf wire primitives, downward builder storage, facets,
explicit verification, nested value messages, repeated numeric and text/byte
values, owned scalar/vector C-string loading, and nested pointer values.
Its implicit field-number fixture checks `ZfStruct` order, a reserved `Skip`
position, facet reordering, and wire round trips without declared field IDs.
The facet fixture exercises inherited and overridden framing configuration
with separate reflected field metadata. It also checks an explicit update
filter without compacting field numbers; `ZpbIOTest` compares filtered
builder, generic stream, and Tx stream output under a prefixed facet.
Direct handler calls cover default and custom facets, construction, placement
construction, and heap allocation through a concrete Zpb descriptor type.
The same fixture checks an all-explicit layout with an ID on its skipped field
and confirms that an old occurrence at that ID is ignored on load.
Raw, `ZuPtr`, `ZuRef`, `ZmRef`, and trait-compatible non-framework pointer fields
have duplicate-merge and replacement fixtures. The non-framework wrapper is
also exercised in repeated message and map-value fields, with duplicate-key
replacement and pointee destruction; constructor pointers and a
finite recursive chain are covered. Enum values and repeated native message
fields have focused fixtures, including raw and smart-pointer elements.
The required-pointer fixture also fails after an earlier field was written:
`save` publishes no span, a retry remains failed, and pooled output cannot
detach the partial message. Map fixtures cover tree/hash storage, duplicate
and unordered entries, native defaults, owning binary values with embedded zero
bytes, nested values, custom facets, pointer cleanup, built-in decimal values,
and application ADL-mapped values.
Canonical mapped scalar/vector fixtures cover 128-bit integers, fixed,
decimal, time, and date-time values, including duplicate-message merge into
constructor and mutable fields. Mapped native roots and an application
`ZpbTransform` wrapper are checked with a custom facet and nested UDT field.
The built-in decimal mapping is also exercised through a UDT field.
`testMappedCtor` checks a custom `ZpbTransform` whose wire message has a
read-only constructor field. Two occurrences preserve the first constructor value
and merge writable fields; typed verification, loading, and re-encoding
agree. Verification does not invoke native mapping conversion or validate its domain.
Boundary fixtures accept native fixed/decimal limits and zero time values,
and accept structurally valid fixed/decimal wire values independently of native domains.
`ZiIP` has fixed-wire IPv4 and IPv6 root fixtures, nested and repeated UDT
coverage, and address-length verification cases.
Fixed and dynamic bitmap fixtures cover root, nested, and repeated values,
zero bitmaps, and native word-count verification.
`ZmBitmap` fixtures cover empty, finite, and open-ended ranges through the
separate range-string mapping.
Repeated message fixtures check that a custom facet reaches every child,
including exact rendered wire order, verification, and loading.
The map fixture makes the same exact-wire check for a nested map value.
`ZpbTest` also checks native `oneof` selection through `ZuUnion`, including
same-type alternatives, duplicate message merge, alternative replacement,
required presence, and malformed wire types. It rejects malformed discarded
alternatives while allowing a missing required field in an alternative that
is later replaced. A rendered custom facet selects only one field inside an
alternative; the test checks its exact wire, verification, and loading.
The merge fixtures include an owned non-framework pointer, appended repeated
values, and a getter-only constructor field which retains its first constructed
value. They exercise both mutable and constructor-supplied containing objects.
An ordinary singular nested message with a read-only constructor field is
also merged across wire occurrences through pointer, value, and constructor
parent storage forms. The pointer fixture also checks that a present split
child survives `Reset` handling and an empty update clears it.
`testAdapterRules` also covers move-only constructor arguments and ensures
`Reset` never deletes a getter-only pointer during update.
`PB::ZigZag` has fixed-byte scalar and repeated fixtures, packed input,
round trips, narrowing rejection, and native null-sentinel acceptance.
`PB::Fixed` has fixed-byte signed/unsigned 32- and 64-bit fixtures, packed
repeated input, and malformed wire/packed-length rejection.
`PB::Packed` has exact-byte output fixtures for ZigZag integers, fixed-width
integers, floats, bool vectors, and narrow unsigned vectors, plus load and
empty-vector checks.
Reflection fixtures preserve explicit packed true/false through descriptor
encoding, import, and proto2/proto3 text emission.
`ZpbIOTest` checks pooled `ZiIOBuf` growth with headroom and tailroom,
detached buffer ownership, explicit limits and framing, generic byte streams,
direct and split Tx output, and admission/send failures.
`ZpbBench` is the correctness setup for small, nested, repeated/map, and
64 KiB byte/string workloads. Each fixture checks typed decoding, typed and
reflected verification, and pooled output against caller-array output. Build
and run only this Zpb target with `make -C zi/test -j4 ZpbBench` and
`./zpb/test/ZpbBench`. For later measurements, the same program accepts
`COUNT small|nested|repeated|large save|pooled|decode|verify|reflect|grow|tx` and
prints CSV. For example, `./zpb/test/ZpbBench 10000 repeated decode` measures
typed decode after the fixture passes its correctness setup; `large pooled`
exercises buffer growth and `large grow` starts a fresh caller array on each
iteration, while `large save` reuses capacity. `large tx` exercises the
`ZiTxStream` buffer handoff callback, including split sends; its correctness
precheck compares complete bytes with caller-array output. This measures a
synchronous handoff callback, not a scheduler or network transport. The
`decode`, `verify`, and `reflect` operations include the codec's field-presence
`ZtScratch` work; for example, use `repeated decode`, `repeated verify`, and
`repeated reflect` for later scratch-inclusive measurements. Those modes do
not isolate scratch allocation from parsing. The downward save path needs no
construction scratch in these fixtures. `../interop/zpbinterop` checks the
same four workload shapes against canonical protobuf and has matching
`parse`/`serialize` measurement commands. No comparative timing has been run.
Do not use the debug configuration for comparative results. See
[`zpbinterop`](../interop/zpb.md) for canonical commands and setup.
`ZpbReflectTest` checks descriptor-set import with
independent wire bytes, borrowed schema lookups, owned input lifetime, and
explicit rejection of malformed or unsupported descriptor metadata. It checks
that standard unrelated file options are discarded while Editions feature
overrides are rejected, plus scalar reflected verification and visitation
against typed decoding,
packed/unpacked values, required presence, unknown fields, and malformed wire.
The writer fixtures compare fixed bytes and round trip decoded values through
the shared downward builder. Message-level writer fixtures check required
presence and canonical nested wire bytes.
Recursive descriptor and payload fixtures cover self references, depth limits,
unresolved names, malformed nested wire, and required-field merge semantics.
Dependency fixtures cover direct imports, missing files, and undeclared
cross-file references. Type-name fixtures reject enum/message collisions in
one file, across files in one package, and within an enclosing message.
Nested descriptor fixtures cover qualified lookup at two levels, required
child payloads, and duplicate sibling names.
Enum fixtures cover file and nested names, reflected wire agreement, unknown
numeric values, and signed 32-bit limits.
Reflected `oneof` fixtures cover declaration indices, same-alternative merge,
alternative replacement, malformed discarded payloads, and field/`oneof`
name collisions in imported and native-exported schemas.
Map-entry fixtures cover descriptor options, duplicate keys, reversed
key/value order, reflected writing, and malformed entries.
Text export fixtures cover the supported scalar, import, nested, enum,
`oneof`, and map descriptor forms.
Focused ownership paths are checked under Valgrind. The native vocabulary
row evidence is listed in [`zi/zpb.md`](../zpb.md).

FlatBuffers coverage is grouped by codec, reflection, and I/O behavior:

- `ZfbTest`: metadata, explicit wire fixtures, verification, buffer construction
  and growth, borrowed views, values, object loading, unions and stream framing.
  Nested loading checks facet propagation through values, inline structs,
  vectors, fixed arrays, and unions, plus allocation/null handling for raw pointers, `ZuPtr`,
  and `ZmRef`, with reference counts and destruction checks.
- `ZfbMapTest`: ADL map dispatch, reference-owned hash/value-tree parity,
  roots and map fields, sorted output and borrowed lookup, unsorted input with
  last-value-wins duplicates, raw/smart/custom pointer lifetimes, nested maps,
  custom facets and transforms, placement construction, schema key metadata,
  and arena growth with existing scratch/output live.
- `ZfbFlatcTest`: span tokenization, schema resolution, emission, invalid
  defaults/names/IDs, and staged-pair replacement rollback with injected I/O
  failure. Full generation/compilation checks live in `../itest`.
- `ZfbReflectTest`: reflection access/writing, recursive BFBS generation,
  union schemas, facet-specific BFBS/FBS and external-schema verification.

`ZfbTest` and `ZfbReflectTest` use the fixture metadata in `ZfbTest.hh`.
`ZfbIOTest` covers pooled ownership, growth, alignment, IP mapping, and
transmit-stream output, including admission and send failure. Canonical
FlatBuffers interoperability remains in `../interop`.

`make test` runs these programs alongside the other Zi unit tests. Rebuild
incrementally with `make -C zi/src -j4` followed by `make -C zi/test -j4`.

## Negative compilation checks

Compile `ZpbTest.cc` with the configured Clang debug compiler/include flags,
`-fsyntax-only`, and exactly one selector per invocation:

- `ZPB_BAD_MIXED`: an explicit field ID beside an implicit ordinary field.
- `ZPB_BAD_SKIP`: an explicit layout whose skipped field lacks an ID.
- `ZPB_BAD_DUP`: duplicate explicit field numbers.
- `ZPB_BAD_RESERVED`: a field number in protobuf's reserved range.
- `ZPB_BAD_ALT_COLLISION`: an ordinary field colliding with a `oneof`
  alternative number.
- `ZPB_BAD_ALT_DUP`: duplicate `oneof` alternative numbers.
- `ZPB_BAD_ALT_RESERVED`: a `oneof` alternative in protobuf's reserved range.

Each invocation must fail at the intended Zpb metadata assertion. The normal
`ZpbTest` build excludes these branches.

Compile `ZfbTest.cc` with the configured compiler/include flags,
`-fsyntax-only` and exactly one of these `-D` selectors per invocation:

- `ZFB_BAD_MIXED`: mixed implicit and explicit field IDs.
- `ZFB_BAD_GAP`: gaps in explicit IDs.
- `ZFB_BAD_UNION`: union field without its preceding discriminator ID.
- `ZFB_BAD_NAME`: duplicate field names.
- `ZFB_BAD_ALIGN`: non-power-of-two alignment.
- `ZFB_BAD_TAG_NAME`: generated discriminator name collision.
- `ZFB_BAD_ALT_NAME`: reserved union alternative name.

Each invocation must fail on its intended metadata assertion. These branches
are excluded from the ordinary runtime program.

The `ZfbIOTest` growth observer forwards to the existing `IOStorage` policy
and reports backing allocations, relocated scratch/output bytes, in-place tail
movement and detach compaction separately. Its paired transmit checks compare
actual bytes and final owner layout for a fitting grown object, and completed-span
insertion plus emitted-buffer allocations for the splitting fallback. Test
reference/observer copies are excluded from those counters. These checks
measure resource behavior separately from latency benchmarks.

`testAdapterRules` checks the shared `ZfStruct` construction/load/update
contract: getter-only constructor fields, writable constructor fields,
non-mutable initial fields, mutable fields, and primary-key fields, through
`ctor`, `alloc`, placement construction, `load`, and `update`.
