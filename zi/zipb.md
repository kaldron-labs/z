# ZiPB implementation contract and coverage

This is the module-level coverage ledger for the work in
[implementation.md](../implementation.md). The typed codec, reflection,
`zprotoc`, pooled output, and canonical interoperability have executable
fixtures described below. A slice reported as implemented is supported by
its named test. The feature-combination and final acceptance audits remain
tracked in `implementation.md`.

`ZiPB` uses `ZfStruct` metadata and lower-level `Zu`, `Zm`, `Zt`, `Zf`, and
`ZiIOBuf` facilities.

## Typed mapping and wire behavior

`ZiPB::AsMessage<"name"> ZiPB_Fmt(Type *)` associates a type with protobuf
metadata. Ordinary Core fields default to numbers in complete `ZfStruct` order,
starting at one. A `Skip` field reserves its number; append new fields to keep
existing numbers stable. If one ordinary field specifies `PB::FieldID<N>`, all
ordinary fields, including skipped ones, must specify IDs. `oneof` members
take their alternative numbers from `AsUnion`. An operation facet may
rename or select fields without changing their numbers. `ZiPB::save(builder,
object)`, `verify<Type>(bytes)`, `handler<Type>(bytes)`, and `load<Type>(bytes)`
have default PB facets. The handler collects constructor fields before
construction and applies writable non-constructor fields afterward. The typed
mapping also covers the nested, repeated, map, union, and built-in value forms
described below.
`save<Facet, Filter>` applies an operation filter to the selected facet without
renumbering its fields; builder, generic stream, and Tx stream overloads use
the same selection.

The raw root is an unprefixed protobuf message. `ZiPB_SizePrefixed<>` adds an
outer four-byte little-endian length equal to the raw message byte count, so
the total frame is four bytes longer than its encoded length. The framed
facet uses the same choice for save, verify, handler, and load.
`ZiPB_SizePrefixed_` accepts a config tail such as
`ZiPB::Config<ZuFacet::Base>`; a derived facet can inherit or override the
framing choice. `ZuStructReflect` separately selects its field metadata.
The builder
returns a span into caller-owned storage; retain that storage through use of
the span and any loaded borrowed string/byte members. `IOBuilder` transfers
ownership of a completed message through `buf()` while preserving caller
headroom and tailroom; generic streams receive bytes and Tx streams flush
pooled buffers directly or split oversized messages. Call `verify` before
`load` or `handler` for untrusted input: it checks wire structure, native narrowing, required fields, and nesting depth.
It does not validate application value domains or reject native null sentinels.
`load` and `handler` are trusted materialization paths; they
still parse the wire but do not repeat value-domain checks or provide a
recoverable partial-load error result. Unknown fields, including
nested groups, are skipped without retention; group end tags must match their
opening field numbers. The support matrix below defines the implemented
protobuf language subset and its exclusions.

`Builder(storage, limit)` bounds the complete output byte count. Reaching the
limit exactly succeeds; exceeding it sets a sticky `Error::Limit` and `save`
returns an empty span. Size-prefixed output counts its four-byte prefix.
Other output errors, including a missing `Required` pointer, are sticky too.
`save` returns an empty span even if earlier fields have already been written;
`IOBuilder::buf()` does not detach a buffer after failure. Callers must check
`failed()` before using a builder's raw `bytes()` view.

For custom `ZiPBTransform` mappings, verification delegates to the wire type's
handler without constructing objects or calling the mapping's `load` function.
Materialization merges duplicate occurrences through writable fields.
Getter-only constructor fields retain their constructed values, following the
[standard ZfStruct adapter rules](../zf/README.md#standard-construction-load-and-update-contract).

Nested value messages measure each
child after writing it into the same destination, verifies child payloads,
merges duplicate child occurrences into mutable or staged constructor values,
and passes the selected facet to child handlers. Verification aggregates child
payloads across duplicate occurrences before checking required fields.
Nested pointer fields use pointee metadata through the same UDT dispatch.
Raw pointers, `ZuPtr`, `ZuRef`, `ZmRef`, and a trait-compatible non-framework
wrapper have focused value, duplicate-merge, and replacement coverage. The
non-framework wrapper also works as a repeated message element and a map
value, including duplicate-key replacement and pointee destruction. A
present empty child allocates a pointee; a null pointer is omitted on output
unless required. Raw-pointer replacement deletes
the old pointee, while wrapper assignment follows native ownership. A finite
recursive `ZuPtr` chain has save, verification, and load coverage.

`ZiPB::AsArray<"name"> ZiPB_Fmt(ArrayType *)` selects a repeated native
message field. Each element uses the containing field number and its own
length-delimited child payload; validation checks each child independently.
Value, raw-pointer, and `ZuPtr` arrays have focused save/load/update coverage.
`ZiPBTest::testArrays` also checks that a rendered custom facet reaches each
child by asserting the reordered child wire and its typed load.
Native raw-pointer array owners delete their elements. Null pointer elements
fail output, and constructor-only arrays stage elements until construction.

`ZiPB::AsMap<"name", KeyCode, ValCode>` selects a native map through ADL.
The handler uses `citer`, `del`, `add`, and `clean` as in `ZfJSON::AsMap`.
Each wire occurrence is an entry message with key field 1 and value field 2.
Entries may arrive in any order; duplicate keys use last-value-wins. Empty
entries use native default key/value values. Key-type restrictions are checked
in metadata, while wire verification applies the ordinary field and native
ingress rules without checking order or uniqueness. Focused tests cover
integer and string keys, tree and reference-owned hash maps, owning binary
values, nested values, custom facets (including exact reordered child wire),
load/update differences, raw and
non-framework-wrapper pointer cleanup, built-in decimal values, and
application ADL-mapped values.

`ZiPB::AsUnion<"name", Alt<"field_name", field_number, Payload>...>` associates a
`ZuUnion<void, Payload...>` with a `oneof` member. Each non-void payload is a
`ZfStruct`-described UDT with its own `ZiPB_Fmt` message format. Alternative
field numbers belong to the union metadata; the containing `UDT` member has
no `PB::FieldID`. The selected alternative emits one length-delimited message
at its number, including an empty message. On input, occurrences of the same
alternative merge, while a different alternative replaces the previous value.
Validation checks the selected payload using the normal message rules. It also
checks discarded alternatives for malformed wire and invalid present values;
missing `Required` fields in a discarded alternative do not fail validation.
Alternative field numbers and names must each be unique, including when the
same payload type appears in more than one alternative.
The selected facet reaches the alternative message: a rendered facet may
select or rename fields inside it without changing the alternative number.
`ZiPBTest::testUnion` checks a facet that emits only the second field of an
alternative, then verifies and loads that wire with the same facet.
Native schema export rejects a field name that collides with another field or
the containing `oneof` name before appending the file. Descriptor import
rejects a field sharing its containing `oneof` name.

`PB::ZigZag` selects protobuf `sint32`/`sint64` encoding for `Int32`/`Int64`
fields and their vector forms. Without it, signed integers use protobuf
`int32`/`int64` two's-complement varints. Repeated ZigZag values emit unpacked
unless `PB::Packed` is selected, and accept either packed or unpacked input.
Invalid native narrowing fails explicit verification. Representable native
null sentinels are accepted. Optional scalar numeric nulls are omitted on save;
a required scalar numeric null fails with `Error::Required`.
`PB::Fixed` selects little-endian `sfixed32`/`fixed32` or
`sfixed64`/`fixed64` for signed/unsigned 32- or 64-bit fields and their
vectors. Repeated fixed-width values emit unpacked unless `PB::Packed` is
selected, and accept either packed or unpacked input; a packed payload must
contain whole elements.

For a writable `Reset` pointer field, an absent update resets the field to its native
default and deletes the previous raw pointee. Unselected or non-`Reset`
fields retain their values.

`verify<Type>(bytes, VerifyLimits{maxDepth})` bounds nested typed messages and
unknown groups. The default permits 64 nested levels. A limit of zero still
accepts a scalar root message. Trusted `handler` and `load` do not apply this
validation limit.

Integer, floating-point, and `PB::BoolVec` vectors emit in native order as
unpacked fields by default. `PB::Packed` emits a nonempty vector as one
length-delimited field containing its elements without individual tags.
Validation and loading accept packed,
unpacked, and mixed segments; loading appends to an existing vector. Numeric
sentinels are preserved as repeated elements.
Native packed output is covered by an independent wire fixture. Reflection
retains the supported `FieldOptions.packed` value, including explicit false,
and emits it in schema text. Native proto3 export writes `packed = false`
for unpacked repeated scalar fields, so the schema describes the native
writer choice. `zprotoc` reads proto2/proto3 packed options and selects
`PB::Packed` for packable proto3 fields by default. Non-repeated and
non-packable uses fail schema validation.
Repeated string and byte spans are also supported. Loaded spans borrow from
the caller-owned input buffer, and explicit empty elements are preserved.

Scalar `CString` materialization copies wire bytes into null-terminated C
storage using the identified `ZiPB.CString` heap. Mutable field
replacement frees its previous value; constructor-field staging frees values
superseded by duplicate wire occurrences. Native owners release the final
pointer with `ZiPB::CStringArg::free()`.

Repeated `CStringVec` values use separate owned null-terminated allocations;
constructor staging frees them until ownership passes to the native object.
Staging uses an identified `ZiPB.CStringVec` array of owning strings.
Mutable loading appends owned elements, and native owners release them with
`ZiPB::CStringArg::free()`.

`ZiPB::Values` defines canonical messages for
`Int128`, `UInt128`, `Fixed`, `Decimal`, `Time`, and `DateTime`. Their scalar
and vector typecodes use the same nested message writer and reader. Duplicate
scalar message occurrences merge fields before native conversion, including
constructor fields. The 128-bit words preserve all 64-bit patterns, including
patterns that are native null sentinels when interpreted alone.

| Message | Fields and protobuf scalar types | Native interpretation |
| --- | --- | --- |
| `Zf.Int128` | `h=1` `uint64`, `l=2` `uint64` | Signed two's-complement bits, high then low |
| `Zf.UInt128` | `h=1` `uint64`, `l=2` `uint64` | Unsigned high then low |
| `Zf.Fixed` | `mantissa=1` `int64`, `ndp=2` `uint32` | `ZuFixed` mantissa and decimal places |
| `Zf.Decimal` | `h=1` `uint64`, `l=2` `uint64` | Signed unscaled 128-bit value at scale 10^18 |
| `Zf.Time` | `sec=1` `int64`, `nsec=2` `int32` | Seconds and nanoseconds |
| `Zf.DateTime` | `julian=1` `int32`, `sec=2` `int32`, `nsec=3` `int32` | Julian day, seconds within day, nanoseconds |

Typed fixtures cover round trips and duplicate merges. Verification checks the
canonical wire messages without imposing native fixed-point, decimal, or time
value-domain constraints. Applications enforce any domain restrictions they
need before materialization. Canonical interop exchanges fixed-point values,
128-bit words, decimals, and temporal fields in both directions. High/low words
preserve all bit patterns through `PB::WireWord`.

The built-in values also work as root objects. An application may return a
mapping with `T`, `save(native)`, and `load(wire)` from the
`ZiPBTransform(Native *)` ADL hook; `AsMapped` then reuses the wire type's
handler for root and nested UDT fields. A custom wrapper has prefixed-root,
nested, and duplicate-message fixtures. Canonical wire metadata is selected
through PB inside mapped adapters, while the surrounding message and optional
root size prefix use the caller's facet.

`ZiIP` uses the canonical `Zi.IP` message with `addr=1` as protobuf `bytes`.
Exactly four address bytes represent IPv4 and exactly sixteen represent IPv6,
in network byte order. The wire value contains no host name or port. Verification
checks the bytes field structure without imposing an address-length policy. Root and nested
UDT round trips have fixed-wire fixtures; repeated UDT values preserve address
family and order.
The canonical interop fixture also exports `Zi.IP`, has protobuf compile the
emitted schema, and exchanges IPv4 and IPv6 values in both directions.
Both reflected and typed verification accept a structurally valid three-byte
`addr`; native address semantics remain the caller's responsibility.

`ZuBitmap<N>` and `ZtBitmap` use the canonical `Zf.Bitmap` message with
`words=1` as protobuf `bytes`. Each 64-bit word occupies eight little-endian
bytes, in increasing bit-index order. Loading copies complete words, truncating to native capacity for fixed
bitmaps; dynamic bitmaps retain the decoded whole-word length. Verification
does not impose native bitmap-size constraints. A zero bitmap is a valid value, including an empty dynamic
bitmap encoded as an empty message. Root, nested, and repeated fixtures cover
the typed mapping. Little-endian targets use a direct byte view; big-endian
targets convert each word into identified temporary storage on save and convert
back on load. The big-endian branch has not been exercised by this build.

`ZmBitmap` uses the separate `Zf.HwBitmap` message with `ranges=1` as protobuf
`string`. The string is the native comma-separated range syntax, including
open-ended ranges for infinitely set bitmaps. An empty bitmap is an empty
message. Verification checks string structure; native loading parses the range text.
Root, nested, and repeated fixtures cover empty, finite, and unbounded values.
Canonical interop compiles the `Zf.Bitmap` and `Zf.HwBitmap` schemas. Fixed
and dynamic bitmap word bytes, an empty dynamic bitmap, and a hardware
bitmap range string exchange in both directions. Reflected and typed verification
accept structurally valid bitmap contents without parsing their native domains.

## Native vocabulary

The source of truth is `ZfFieldTC` in `zf/src/ZfStruct.hh`. `UDT` is dispatched
recursively or through an ADL mapping; all other codes below require native
codec, reflection, schema, and interop coverage. Generated C++ uses the
canonical type selected by a protobuf schema: a `string` cannot identify
`CString` versus `String`, and `int32` cannot identify a native 8- or 16-bit
field. Those native choices are exercised through handwritten metadata and
their exported schemas. Each row below names the focused native and independent
canonical evidence. `testGeneratedNumeric` covers the generated 32/64-bit
numeric spellings; `zprotocconsumer` covers generated strings, bytes, enums,
and repeated forms; `testGeneratedValues` covers generated canonical value
adapters and their vectors. Overall feature-combination acceptance is tracked
in `implementation.md`.

| Scalar code | Vector code | Protobuf representation | Executable evidence |
| --- | --- | --- | --- |
| `CString` | `CStringVec` | `string`, repeated `string` | `testCString`, `testCStringVec`, `testCStringInterop`; generated `string` uses owning `String` storage. |
| `String` | `StringVec` | `string`, repeated `string` | `testFields`, `testTextVectors`, `testScalarVocabulary`, `zprotocconsumer`. |
| `Bytes` | `BytesVec` | `bytes`, repeated `bytes` | `testBytes`, `testTextVectors`, `testScalarVocabulary`, `zprotocconsumer`. |
| `Bool` | `UInt8Vec` with `PB::BoolVec` | `bool`, repeated `bool` | `testRow`, `testBoolVec`, `testBoolVecInterop`, `testGenerated`. |
| `Int8` | `Int8Vec` | signed scalar with checked narrowing; repeated | `testNarrowTypes`, `testVectors`, `testScalarVocabulary`; generator uses `int32`. |
| `UInt8` | `UInt8Vec` | unsigned scalar with checked narrowing; repeated | `testNarrowTypes`, `testScalarVocabulary`; generator uses `uint32`. |
| `Int16` | `Int16Vec` | signed scalar with checked narrowing; repeated | `testNarrowTypes`, `testScalarVocabulary`; generator uses `int32`. |
| `UInt16` | `UInt16Vec` | unsigned scalar with checked narrowing; repeated | `testNarrowTypes`, `testScalarVocabulary`; generator uses `uint32`. |
| `Int32` | `Int32Vec` | `int32`, `sint32` with `PB::ZigZag`, or `sfixed32` with `PB::Fixed`; repeated | `testVectors`, `testZigZag`, `testFixedWire`, `testWireNumbers`, `testGeneratedNumeric`. |
| `UInt32` | `UInt32Vec` | `uint32` or `fixed32` with `PB::Fixed`; repeated | `testNarrowTypes`, `testFixedWire`, `testWireNumbers`, `testGeneratedNumeric`. |
| `Int64` | `Int64Vec` | `int64`, `sint64` with `PB::ZigZag`, or `sfixed64` with `PB::Fixed`; repeated | `testNarrowTypes`, `testWireNumbers`, `testGeneratedNumeric`. |
| `UInt64` | `UInt64Vec` | `uint64` or `fixed64` with `PB::Fixed`; repeated | `testVectors`, `testWireNumbers`, `testGeneratedNumeric`. |
| `Int128` | `Int128Vec` | framework-owned message mapping; repeated | `testMapped`, `testWordMappings`, `testGeneratedValues`. |
| `UInt128` | `UInt128Vec` | framework-owned message mapping; repeated | `testMapped`, `testWordMappings`, `testGeneratedValues`. |
| `Float` | `FloatVec` | `float` or `double` from native width; repeated | `testFloat`, `testFloat32`, `testVectors`, `testScalarVocabulary`, `testGeneratedNumeric`. |
| `Fixed` | `FixedVec` | framework-owned message mapping; repeated | `testMapped`, `testFixedMapping`, `testGeneratedValues`. |
| `Decimal` | `DecimalVec` | framework-owned message mapping; repeated | `testMapped`, `testWordMappings`, `testGeneratedValues`. |
| `Time` | `TimeVec` | framework-owned message mapping; repeated | `testMapped`, `testTemporalMappings`, `testGeneratedValues`. |
| `DateTime` | `DateTimeVec` | framework-owned message mapping; repeated | `testMapped`, `testTemporalMappings`, `testGeneratedValues`. |

`UDT` includes message values, recursive values, enums, unions/`oneof`, maps,
bitmaps, IP addresses, and custom ADL mappings. These have separate executable
coverage; they are not a substitute for any vocabulary row. The mapped-value
shapes above are fixed for the typed mapping. The typed operation-axis audit
is recorded in `implementation.md`.
The table records representation families and exercised paths, not every
cross-product of native storage, facets, and schema syntax.
The generated `ValueRow` interop fixture also has canonical protobuf parse
each of the six numeric/temporal vector forms, append an independently populated
element, and serialize both; generated ZiPB verifies and loads the result.
It likewise exchanges generated bitmap and hardware bitmap vectors and maps
with canonical protobuf in both directions.

## Schema and wire boundary

Reflection imports the supported descriptor subset from canonical
`FileDescriptorSet` using `ZiPB::Reflect::Schema`. The schema owns its input
bytes; file, message, and field lookup results borrow that owner. Explicit
`verifySchema` rejects malformed descriptors, duplicate identities, and
unsupported semantic constructs, including public/weak
imports, enum options, other message options, and group fields.
Standard `FileOptions` that affect other language generators are parsed and
discarded; Editions feature overrides and custom file options are rejected.
Duplicate protobuf type names across enums and messages, within a package or
enclosing message, fail descriptor verification; separate packages remain
independent.
`Schema::file(name, syntax)` checks the caller's proto2/proto3 choice for the
selected file; whole-set verification uses each imported file's own syntax.
Canonical interop also checks a custom facet's renamed field, stable numbers,
reserved `Skip` position, and complete schema when the facet omits a field.
Direct dependency names are retained and resolved within the descriptor set;
cross-file message references require a declared direct import. Fully
qualified message references, including nested and recursive references, are
resolved against the owning schema. `Reflect::emitProto` prints the supported
imported descriptor subset, including package/imports, nested messages, enums,
`oneof`, and map syntax. `emitProto` takes an explicit `Syntax::Proto2` or
`Syntax::Proto3` argument and rejects a choice that conflicts with the selected
descriptor file. The [ZiPB interop fixture](interop/zipb.md) checks
canonical parsing, descriptors, and payload interchange for import, map, and
`oneof`. `Reflect::exportSchema` now exports scalar, ordinary and repeated
message, map, and `oneof` fields from a native facet. It takes field numbers
from the typed handler, including implicit `Skip` reservations. Facet filters
retain the complete schema while selected `PB::ID` properties rename fields.
Exported `Skip` slots become protobuf reserved numbers. Imported message
descriptors retain reserved ranges and names, and `.proto` output emits them.
Generated `AsMessage` formats carry those reservations through native export.
Native export takes an explicit file name, package, and `Syntax::Proto2` or
`Syntax::Proto3` value. The C++ mapping carries no source file or syntax.
The resulting `DescSet` borrows the supplied file-name and package strings;
keep them alive until the descriptor has been encoded or emitted, or retain
owned copies when keeping the descriptor longer. `Schema` owns a copy of its
encoded descriptor input and its lookup handles borrow that `Schema`.
Fields marked `Required` become required only in proto2 output; proto3 output
uses ordinary fields. Native export resolves message and enum references in
other packages after the app has added their schemas to the same `DescSet`.
It records each dependency file once as an import; unresolved references fail
without adding the new file. External enum names and numbers must agree with
the supplied declaration. Imported proto2 scalar defaults
are retained and emitted, with string and byte escaping checked by canonical
protobuf. An explicit empty string default and an absent default have the same
native `ZuCSpan` null representation, so that presence distinction is lost.
Native proto2 facet export emits supported `Deflt` scalar values, including
escaped byte defaults. Local signed integer `Enum` maps export file-level or
current-message enums for singular, repeated, and map-value fields, including
proto2 enum defaults; canonical protobuf parses the emitted schema. Proto3
output omits `Deflt` options. The supported canonical cases and remaining
combination audit are recorded in the vocabulary table and `implementation.md`.
Typed and reflected ZiPB wire paths accept unknown signed 32-bit enum numbers
and expose their numeric values independently of source syntax. The C++ mapping
does not carry a proto2/proto3 version. This differs from canonical
[proto2 closed-enum behavior](https://protobuf.dev/programming-guides/enum/):
canonical parsing leaves an unknown enum field unset and places its value in
the unknown-field set. The `testNativeEnum` interop fixture checks both results
from the same wire bytes. ZiPB does not retain an unknown-field set, so it
cannot reproduce canonical proto2 presence or unknown-field storage here.
The explicit `zipbinterop` test also checks typed native map/`oneof` payloads
in both directions against canonical dynamic messages and ignores unknown
fields on native load. Its `zprotoc`-generated fixture exchanges map,
message-valued `oneof`, repeated signed fields, and repeated bool with
canonical messages.
`zprotoc` accepts supported proto2/proto3 messages with an optional
package and bare `bool`, 32/64-bit integer, fixed integer, `float`, `double`,
`string`, or `bytes` fields, plus repeated numeric, string, byte, and message
fields. Generated strings, byte arrays, and repeated containers
own storage through identified framework heaps. Singular and repeated same-file or
directly imported message fields support references; generated `ZuPtr` fields own
their pointees through concrete `ZmHeap` types.
File and nested enums generate constants and Zf name maps with explicit sparse
and negative `int32_t` numbers. Singular and repeated enum fields preserve
their numeric wire values. Generic Zf text enum scanning treats negative map
results as failures, so negative enumerator names are not accepted by those
text loaders; the ZiPB numeric wire path preserves them.
Nested message declarations resolve through enclosing scopes and generate
flattened C++ names. Local and imported nested message fields compile and
exchange payloads; flattened-name collisions fail before publication.
Proto3 `oneof` groups with message alternatives generate a `ZuUnion<void, ...>`
member and an ADL `AsUnion` format. Forward alternative types are defined
before the containing message. Scalar alternatives and cyclic by-value
alternative dependencies are rejected. Repeated occurrences of the selected
message alternative merge; a different alternative replaces it.
The generated consumer also loads two independent `oneof` groups with
interleaved fields and separate alternative switches.
Canonical dynamic protobuf and ZiPB typed/reflected verification exchange
that two-group shape in both directions and accept the interleaved switch wire.
Proto3 `map<key, value>` declarations generate descriptor map entries and
identified `ZmRBTreeKV` storage with ADL `AsMap` selection. Supported keys are
protobuf bool, integer and string keys; values include scalar, string, bytes,
enum and message types. Signed and fixed wire forms use the corresponding
protobuf properties. Map entries may arrive in either field order; duplicate
keys use the last value. A generated bytes-map consumer compiles and checks
owning binary values and duplicate-key replacement. Imported message values
compile in separate consumers.
It emits a guarded native header and source with explicit PB field numbers.
`zi/itest/zprotoctest` runs the tool and a separately compiled consumer,
checking stable text/descriptor output, payload interchange, invalid-schema
rejection, and preservation of prior output on a handled publication error.
The generated consumer also re-exports a message with repeated `float` and
`double`: the reflected field types retain their distinct element widths.
The runnable [`zi/example` example](example/README.md) generates a small schema
and exchanges its message with handwritten metadata using implicit field IDs.
`--descriptor [--file name]` accepts a validated FileDescriptorSet and selects
its last file by default. `--export MACRO` applies a validated C++ export macro
to generated declarations and metadata. Text imports search relative to the
source, then each `-I` directory; direct dependency headers are included.
Package and import declarations may precede or follow message/enum declarations.
`--syntax proto2|proto3` selects the expected version for the input file and
fails before output replacement if its declaration disagrees. Generated C++
does not retain that source-version choice.
An omitted declaration selects proto2, matching canonical `protoc`.
The imported consumer builds and passes a payload round trip, and text and
descriptor-set imports generate identical output. Missing imports and import
cycles fail before publication. Generated helper-name collisions across reachable
imported headers and namespace/type collisions also fail before publication.
The imported-enum consumer uses proto2 on both sides. A proto3 message cannot
use a proto2 enum directly, including as a map value; `zprotoc` rejects this
before replacing generated files. `Reflect::verifySchema` rejects the same
cross-syntax enum reference in a descriptor set.
Duplicate fully qualified declarations in text input fail during resolution,
before output replacement. Descriptor-set duplicates fail schema verification.
The generator recognizes `Zf.Int128`, `Zf.UInt128`, `Zf.Fixed`, `Zf.Decimal`,
`Zf.Time`, and `Zf.DateTime` after validating their field names, numbers,
scalar types, and simple message shapes against the table above. Generated
singular and repeated fields use native types and the existing mapped codecs;
`Zf.Fixed` also works as a map value. The generated canonical declarations do
not duplicate native wire messages. The separately compiled `zprotocvalues`
consumer exchanges all six values with a handwritten PB mapping. The generator
also recognizes `Zi.IP`, `Zf.Bitmap`, and `Zf.HwBitmap` after validating their
single-field shapes. It uses `ZiIP`, dynamic `ZtBitmap`, and `ZmBitmap` for
singular, repeated, and map values, with value-owning `AsArray` metadata for
repeated fields. Protobuf `Zf.Bitmap` does not carry a fixed width, so generated
code selects the dynamic native bitmap. The consumer imports both canonical
packages and exchanges these fields with handwritten PB mappings.
Proto2 `optional`, `required`, and `repeated` fields generate with decimal
integer, floating-point (including infinities), bool, enum, escaped string
(including Unicode), and escaped byte defaults; missing required fields fail typed verification
and output. Generated nullable numeric members without explicit defaults start
at Z null sentinels. NaN defaults are rejected as native null sentinels.
Generated proto2 message-valued `oneof` groups use the same `ZuUnion` mapping;
the compiled consumer checks replacement and discarded-payload verification.
Reflection retains proto3 `optional` descriptors and emits their text syntax,
including the synthetic oneof rule. `zprotoc` rejects proto3 optional text
and descriptor input before publication: the native `ZuNull` policy does not
recover every explicit-presence distinction. The compiled generator fixtures
cover all standard protobuf scalar spellings and the recognized `Zf`/`Zi`
canonical built-in messages. Schema-only input cannot recover native C-string
ownership, narrowed integer widths, fixed bitmap width, or application facets;
unsupported semantic constructs fail before publication.
For supported scalar descriptors, `Reflect::verify` checks payload wire types,
width, required presence, packed segments, and unknown-field structure;
`Reflect::visit` invokes a callback with decoded numbers or transient string/
byte spans. `Reflect::write` emits supported scalar fields through the shared
downward builder, with a sequence overload that preserves input order. Its
message overload checks field ownership and required presence before writing;
message fields accept encoded child payloads. `Reflect::writeNested` instead
writes one message field from child values directly into the same backward
builder; the caller supplies the parent field and schema and writes later
parent fields first. It checks child field ownership and required presence.
File and nested enum names
resolve through the schema; enum payloads use signed 32-bit varints and permit
unknown numeric values. Map-entry messages retain `map_entry` metadata;
entry visitors and writers preserve wire order and permit duplicate keys.
An owning reflected message graph and a whole-tree writer remain outside this
API. `oneof` declarations and field
indices are retained; reflected
nested-message verification applies last-alternative selection and checks
discarded alternatives for malformed wire. The schema-aware
verifier checks nested messages with a depth bound and merges duplicate singular
message occurrences before checking required child fields.

| Construct | Intended treatment | Status |
| --- | --- | --- |
| Proto2/proto3 messages, scalar/string/bytes fields | Implement | Typed, reflected, generated, and canonical fixtures pass; the vocabulary table names each row's evidence |
| Implicit ordered or all-explicit field numbers, names, defaults, required fields | Implement and diagnose invalid metadata/schema | Implicit-ID, default, and required fixtures pass; seven negative compilation selectors exercise invalid field-number metadata |
| Optional/presence | Represent only distinctions available from native `ZuNull` conventions; document lossy cases | Reflection/interop fixtures pass; `zprotoc` rejects proto3 optional codegen explicitly |
| Unknown enum numbers | Keep signed 32-bit numeric values in typed/reflected fields independent of source syntax | `testEnum` and canonical `testNativeEnum` pass; proto2 closed-enum presence/unknown-set behavior differs as documented above |
| Repeated, packed/unpacked input, maps, enums, `oneof` | Implement with protobuf merge/order semantics | Typed, reflected, generated, and canonical fixtures pass the documented operation-axis audit |
| Packages, imports, nested/forward/recursive types | Implement in standalone `zprotoc` | Generated consumers and recursive/import fixtures pass; reachable-import type/helper/namespace collision checks run before output staging |
| Unknown fields | Skip recursively without retention | Typed and reflected fixtures pass, including schema-evolution interop |
| Extension declarations/values, typed groups, services/RPC | Reject as unsupported schema semantics | Explicit text diagnostics; descriptor verification names extensions, groups, and services |
| Custom options, Editions features, general well-known-type adapters | Reject semantic use unless explicitly added to the supported matrix | Text and descriptor diagnostics name recognized options/Editions; general well-known-type adapters remain excluded |
| Source comments, formatting, standard unrelated file options | Discard on import/export | Text and canonical descriptor inputs produce identical C++ with those options; exported schema omits them |

An explicit verifier checks untrusted wire structure, native narrowing,
required fields, and nesting depth. Trusted handlers and `load` do not revalidate. Borrowed wire
views, unknown-field retention, per-message
indexes, owning dynamic message graphs, pointer-cycle scans, RPC/gRPC, and
application migrations are outside the requested implementation.

Reference specifications: [protobuf encoding](https://protobuf.dev/programming-guides/encoding/),
[proto2](https://protobuf.dev/programming-guides/proto2/),
[proto3](https://protobuf.dev/programming-guides/proto3/), and
[field presence](https://protobuf.dev/programming-guides/field_presence/).
The canonical tool reports `libprotoc 36.1` on this host; the focused
`zipbinterop` executable passes 26 TAP subtests with that runtime.
