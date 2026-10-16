# zflatc implementation plan

## Required outcome

Build and install a command-line tool named `zflatc`, implemented in
`zi/src/zflatc.cc`. It parses `<name>.fbs` and emits `<name>.hh` and
`<name>.cc`; `--alt` selects `<name>.hpp` and `<name>.cpp`. The reference to
`flatc` in the request is interpreted as `zflatc`; canonical `flatc` remains
the separate interoperability tool.

Generated code declares native C++ structs, canonical Z member types,
`ZfStruct` metadata, and the ADL mappings needed by `ZiFB`. It uses the existing
typed codec rather than generating another FlatBuffers wire implementation.
FlatBuffers field names are snake-case; C++ data members are camel-case.
Every generated string and array member has a named `ZuDerive` type backed
by the appropriate heap-identified Z container. Default heap IDs are
`"<name>.<member>"`, where `<name>` is the qualified C++ containing type
expressed with dots and `<member>` is the camel-case C++ member spelling
(resolved Q002). For example, `example::Record::displayName` uses the heap ID
`"example.Record.displayName"`.

Object references use `ZuPtr<T>` by default. `--ref <type>` switches them to
`ZmRef<T>` and derives generated C++ structs from the specified reference-counting
base, normally `ZuObject`, `ZuPolymorph`, `ZmObject`, or `ZmPolymorph`
(resolved Q004).

Implement the requirements below in the stated dependency order. All Q001–Q016
decisions are settled and binding; their permanent records remain at the end.
This document prescribes implementation, not further design approval.

## Prerequisite: completed ZiFB adapter refactor

Implement against the completed target structure in `refactor.md`. Treat that
refactor and its acceptance gates as prerequisite work, not work to repeat
inside this plan. Use the post-refactor working tree and preserve concurrent
changes. This plan does not certify that the refactor's gates have passed.

Use the single `ZiFB_Fmt` ADL hook and `ZiFB::As<O>` descriptor selection path.
Access object metadata and operations through
`typename ZiFB::As<O>::template Handler<O, Facet>`. Do not restore retired
`ZiFBType`, `Type`, standalone `Meta`/`Layout`/`Handler`, or typed free
`saveObject`, `writeInline`, `saveUnion`, and composition functions, including
as generated aliases or compatibility wrappers. Native/wire conversion
remains separate through the retained `Mapping`/`Transform`/`ZiFBTransform`
facilities.

Codec, view, handler, alignment, and schema API facet parameters default to
`ZuFacet::FB`. Dependents use the same default, including explicit facet
arguments needed to select an operation filter. The generated canonical `FB`
facet has the same field list as Core. Existing native FlatBuffers types also
declare FB explicitly; an undeclared facet has no fields. Keep complete Core wire layout intact
and propagate the selected facet through nested native loading, including
pointer allocation, collection elements, and union alternatives, as corrected
in `refactor.md`.

### Completed loading corrections (2026-10-11)

The following ZiFB work is implemented and verified; use it as the generator's
baseline rather than reimplementing it:

- `nativeValue<T, Facet>` selects the nested handler with the containing
  facet. Value destinations use `ctor()`; pointer destinations use the
  pointee handler's `alloc()` when present and receive null when absent.
  Raw-pointer ownership remains with the caller; `ZuPtr` and `ZmRef` take
  ownership through their normal assignment/construction paths.
- `NativeArray` carries the facet into element materialization, and native
  union loading carries it into both value and pointer alternatives.
- Inline struct field access maps facet field declarations to their matching
  Core fields before looking up wire offsets. Native loading honors the
  selected facet while wire layout remains the complete Core layout.
- `ZiFBTest::NestedLoadTest` covers nested values, inline structs, table
  vectors, value/pointer union alternatives, raw pointers, `ZuPtr`, and
  `ZmRef`; it checks null fields, repeated owning loads, reference counts,
  independent pointees, and destruction. A facet omitting a field proves
  loading does not reset to Core. Materialized values survive source-buffer
  release. Object-vector elements must be nonnull; absence belongs to the
  containing field.
- The `SaveOnly` fixture now saves with `ZfFieldFilter::All`: its read-only
  getter has no constructor/key metadata and is intentionally omitted by the
  default `Save` filter. Preserve that filter contract.

Validation used the retained build configuration: incremental `zi/src` and
`zi/test` builds, followed by `prove ./ZiFBTest ./ZiFBReflectTest ./ZiFBIOTest`
from `zi/test` (all three passed). `libtool exec valgrind` on `ZiFBTest`, run
outside the sandbox with full networking access, reported zero errors and zero
definitely, indirectly, or possibly lost bytes. No build reconfiguration or
dependent builds outside `zi` were performed. This verifies the loading
corrections, not generator completion; generated fixtures and all four
`--ref` base variants still require the checks below.

## Existing facilities and constraints

- `zi/src/ZiFB.hh` provides `AsTable`, `AsStruct`, `AsEnum`, `AsUnion`,
  descriptor-owned handlers, typed save/load/view/verify, native value mappings,
  and field-ID handling. `AsStruct` denotes an inline FlatBuffers struct.
  Handler `SchemaFields` describes complete facet-overlaid schema fields;
  `AllFields` describes active selected fields. Table handlers own stable
  numeric IDs; Core struct handlers expose layout, size, and alignment.
  `FB::ID` overrides a wire name; the default uses `ZtCase::camelSnake`.
  `FB::FieldID` specifies a numeric wire slot, distinct from that wire name.
  A union consumes its discriminator slot as well as its payload slot.
- `zi/src/ZiFBReflect.hh` provides canonical reflection declarations,
  `saveSchema` (BFBS), and `saveFBS` (schema text). There is no FBS text parser
  in these facilities. Reuse their vocabulary and mapping rules. Schema
  emission consumes the selected handlers' schema fields, IDs, and layout;
  reflection/BFBS/FBS remain in this header, not in core handlers. Dynamic
  `Reflect` operations remain distinct from typed handler operations.
- `zi/test/ZiFBTest.hh` and `ZiFBReflectTest.cc` supply fixtures for native
  values, recursive tables, defaults, deprecated slots, facets, unions,
  alignment, and schema emission.
- `configure.ac` currently confines canonical FlatBuffers dependencies to
  explicit interoperability targets. Keep the ordinary `zflatc` build and
  generated code independent of canonical FlatBuffers (resolved Q001).
- Apply `GUIDELINES.md`: GNU C++2b, Z containers and formatting, tunable heaps,
  SFINAE/ADL, hard tabs, concise names, direct includes, and no compatibility
  shims. Generated code follows the same rules, including the authoritative
  28-byte naming limit; report unrepresentable public names (Q005).

## Implementation sequence

### 1. Establish the mapping inventory and CLI

Create a feature matrix covering the FBS features Z can emit through
ZiFB/ZfStruct and `saveFBS`. Use the existing canonical C++ types for every
`ZfFieldTC`; do not invent replacement representations (resolved Q007).
Accept all `saveFBS` output; FlatBuffers schema features that Z cannot emit
are out of scope (resolved Q001). Record the
mapping for each supported feature and diagnose out-of-scope input; do not
silently drop fields, attributes, defaults, or union alternatives.

Inventory the mappings directly from `ZfFieldTC`, ZiFB's value mappings,
`SchemaOut`, and `FBS` emission over the refactored handlers. For every emitted
schema construct, record
its native type, field typecode/properties, ADL mapping, and materialization
path. Repair demonstrated gaps needed for these mappings in the owning ZiFB
policy/handler or retained value-conversion layer; do not expand supported FBS
features, add replacement types, or reintroduce a parallel metadata/codec layer.
Keep metadata-only instantiation independent of native constructors/setters.

Preserve schema declaration order for unrelated definitions and table field
order for numeric IDs. Order inline definitions by their inline dependencies;
forward-declare tables before emitting references. Convert dotted schema
namespaces to C++ namespaces while retaining the original dotted ADL wire
names. Use schema type names for public C++ type names and `ZtCase::snakeCamel`
for field names, subject to Q005's rejection rules. Use original numeric enum
and union values, not sorted positions. Check in representative expected
declarations as generation fixtures; no additional design approval is required.

Implement this CLI:
`zflatc [--alt] [--ref <type>] [--export <macro>] [-I <dir>] [-o <stem>] <name>.fbs`,
Provide help, nonzero error status, and filename/line/column diagnostics. Accept options
before or after the input path. Without `-o`, replace the input's `.fbs`
extension, placing outputs alongside the schema. `-o` overrides the output
stem, including its optional directory; relative stems resolve from the current
directory (resolved Q010). Thus `zflatc fbs/foo.fbs` emits `fbs/foo.hh` and
`fbs/foo.cc`, whereas `zflatc fbs/foo.fbs -o foo` emits `foo.hh` and `foo.cc`
in the current directory. An input in the current directory naturally emits
there too. Permit repeated `-I <dir>` options for includes within schema files;
search those directories in the order the options appear, without sorting or
reversing them. The generated source includes the selected header extension;
generated include dependencies
also honor `--alt`.
Generate files only for the requested input schema. Parse included schemas
for resolution, but do not generate their files or duplicate their declarations.
Instead, emit corresponding quoted C++ header includes: if `foo.fbs` includes
`bar.fbs`, `foo.hh` includes `"bar.hh"` (`foo.hpp` includes `"bar.hpp"` with
`--alt`). Dependencies are generated by separate invocations (resolved Q013).
`--export <macro>` supplies the export macro for generated declarations and
metadata, for example `--export ZiAPI`; the default is no export macro
(resolved Q011).

### 2. Implement parsing and schema resolution in zflatc.cc

Implement a one-shot pipeline in `zflatc.cc`: parse CLI → read/tokenize/parse
the root and its includes → resolve symbols → validate native/wire mappings →
emit both staged outputs → replace destinations. Keep implementation types
at file scope and make internal functions `static`; keep the program entry
point thin. Do not expose a new public parser library.

Use a tokenizer and recursive-descent parser with file-backed source storage
and borrowed token spans. Follow the span-scanning patterns in `ZfJSON`,
`ZfYAML`, `ZfTOML`, and `ZfCf`. Use their concise naming conventions throughout
the parser, including `bok`/`eok`, `eon`, and `eos` for boundary helpers; avoid
verbose replacements for established names. Use `ZuMatcher` for multiple keyword choices,
`ZuBox` for numeric conversion, and the Z UTF facilities for Unicode escapes.
Decode escaped strings in place when owned storage is required; retain borrowed
spans for tokens. Retain source buffers until emission and diagnostics
are complete. Give each schema file a stable identity and each declaration a
file identity plus source offset; derive line/column diagnostics from those
locations. Keep wire names, converted C++ names, and numeric field IDs as
distinct data.

Represent files, objects, fields, enum values, union alternatives, attributes,
and type expressions as Z-owned records. Store references between records by
stable handles rather than pointers into growing arrays. Use `ZtArray` for
ordered collections and `ZmHash` for file/symbol lookup, with named
`ZuDerive` container types and `zflatc`-identified heaps. Size storage from
actual input or normal container growth; avoid per-token copies, unexplained
fixed capacities, and an STL-owned schema graph. Tag type expressions so
resolution can distinguish scalars, named types, vectors, and fixed arrays.

Parse comments, identifiers, qualified names, string escapes, integer and
floating literals, namespaces, includes, tables, inline structs, enums, unions,
vectors, fixed arrays, defaults, attributes, and schema-level declarations
according to the mapping inventory. Numeric handling must retain widths, signed
limits, negative zero, hexadecimal floats emitted by `saveFBS`, and special
floating values where supported.

Resolve relative includes from the including file's directory, then search
repeated `-I` directories in command-line order. Normalize file identities,
parse each file once, and distinguish an include encountered again from an
active include cycle. Retain include provenance for generated header paths;
search directories are not copied into those paths. Replace the included
schema extension with the selected C++ header extension.

Resolve symbols in a separate pass: duplicate definitions, namespace lookup,
forward references, recursive tables, and illegal cycles of inline values. Preserve source
locations through resolution. Derive implicit field IDs, validate explicit
IDs, and account for union discriminator slots without treating them as an
extra native data member.

Finish schema validation before writing outputs. Diagnose invalid member-name
conversion, collisions, unsupported representations, and file failures at
their source locations. Validation failure must leave existing outputs intact.
Overwrite existing outputs by default, after schema validation. Stage both
generated files before replacing either destination, and handle replacement
errors so failure does not leave a successful-looking half-generated pair
(resolved Q014).
Use temporary files in the destination directories, check writes and closes,
and preserve existing destinations until both staged files are complete.
Restore the previous pair on a handled replacement failure, removing newly
created destinations if there was no previous file. Report failures and clean
temporary files; do not claim crash-atomic replacement of two separate files.

### 3. Generate canonical Z declarations and metadata

Keep parsing, resolution, and emission separate within `zflatc.cc`; use
file-scope implementation types/functions unless reuse demonstrably requires
a library API.

Scope generated allocation helpers by public type under the reserved
`Zflatc_` implementation namespace (Q015), so separate schema files sharing a
namespace cannot reuse file-local numeric helper names. Keep internal helper
names concise and distinct from member container names and `ZuDerive_`'s `Base`.

Emit the header with modelines, generated-file provenance, guard, direct
dependencies, namespace blocks, named container types, public data structs,
metadata, and ZiFB ADL declarations. Declare each generated object's metadata
with `ZfStruct(API, (Type, FB), ...)`: the Core and canonical FB facets share
the recovered field list. `API` is the selected export macro or an empty macro
argument. No source-specific facets are emitted.

Emit the source with its selected header and, in the corresponding namespaces,
`ZfStructImpl(Type)` and `ZfStructImpl(Type, FB)` for each generated object.
Pair generated `ZtEnum` declarations with `ZtEnumImpl` definitions in the
source using the established namespace convention. These existing macros
provide the declaration/implementation split; do not generate a second codec
or unnecessary wrapper APIs. Do not emit implementations for declarations
owned by included schemas or built-in Z mappings.
Apply `--export <macro>` to generated declarations requiring export decoration
and the export argument of Z metadata macros. With no option, emit no export
macro. The consumer supplies the macro definition through its normal component
headers/build configuration; do not invent a definition for it.

Use these mapping rules:

- Use the existing canonical C++ types for every `ZfFieldTC`, including scalar
  widths, signedness, and vector element types (resolved Q007). Derive FBS
  scalar coverage, including `float` versus `double`, from existing Z emission
  and its canonical mappings.
- Map strings to named `ZuDerive` wrappers over
  `ZtString<ZtStringHeapID<"<name>.<member>">>`.
- Map vectors to named `ZuDerive` wrappers over
  `ZtArray<Elem, ZtArrayHeapID<"<name>.<member>">>`. Use canonical scalar/value
  elements, named owning containers for string/array elements, and the selected
  smart-pointer type for table elements. Apply the same member heap ID, with
  no suffix, to inner owning
  string/array storage too; an identified outer vector alone is insufficient
  (resolved Q003).
- Handle fixed-length FBS arrays as a distinct wire contract using the
  existing canonical Z representation and traits that retain the schema
  extent; do not replace them with an unconstrained vector or ad hoc C array.
- Generate snake-case to camel-case names using `ZtCase::snakeCamel`.
  Require the FlatBuffers snake-case convention
  for field names. Reject keywords, reserved/overlong names, and snake-to-camel
  collisions; do not escape or rename them (resolved Q005).
  Preserve the exact wire name with `FB::ID`
  whenever the default reverse conversion would differ. Check keywords,
  reserved names, acronym/digit behavior, and converted-name collisions.
- Emit `ZfStruct` field typecodes and representable properties, including
  numeric `FB::FieldID`, defaults, required/deprecated fields, enum maps, and
  alignment. Keep field order independent of any symbol-table sorting.
  Use the existing canonical `FB` facet (`ZuFacet::FB`, declared by
  `ZuStructFacet(FB)` in `ZiFB.hh`) for generated FlatBuffers metadata.
  Reconstructing source facets is not required (resolved Q009).
- Emit `ZiFB::AsTable<"wire.Name"> ZiFB_Fmt(Type *);` for tables,
  `ZiFB::AsStruct<"wire.Name"> ZiFB_Fmt(Type *);` for inline structs,
  `ZiFB::AsEnum<"wire.Name"> ZiFB_Fmt(Map *);` for enum maps, and
  `ZiFB::AsUnion<"wire.Name", ZiFB::Alt<...>...> ZiFB_Fmt(Type *);` for
  unions, in the corresponding C++ namespace. Preserve original qualified wire
  names, union alias names, and discriminant values; diagnose unsupported sparse
  representations. Let `As` select the policy; generate no second type hook.
- Recognize ZiFB's built-in value schemas by qualified wire name and validate
  their structure, then emit canonical types such as `int128_t`, `ZuFixed`,
  `ZuDecimal`, `ZuTime`, and `ZuDateTime` instead of redundant surrogate structs
  (resolved Q006). Reject recognized names with incompatible definitions;
  generate ordinary structs for other schema types. Recovery of arbitrary
  application transforms is out of scope; introduce no custom attributes for it.
- Use `ZuPtr<T>` for object references, including table-vector elements, by
  default. With `--ref <type>`, use `ZmRef<T>` and publicly derive generated
  C++ structs from the specified reference-counting base (resolved Q004).
  Emit the direct pointer/base includes and preserve the selected policy
  throughout nested references. Keep inline wire values inline; inheritance
  changes native representation, not ZiFB's wire layout.
- Verify ZiFB materialization into both pointer representations, null handling,
  recursive schema declarations, and deterministic destruction using the
  completed facet-aware `ctor()`/`alloc()` paths above. Do not add a parallel
  loader or reinstate the former pointer assertion/Core reset. Use the
  established concrete-type `ZmHeap` pattern for allocated generated objects;
  reference-counting bases do not replace allocation policies. Generated
  strings and arrays must own materialized data; borrowed ZiFB views remain
  available through the existing codec.
  Keep table/union reference descriptors shallow so self-referential and
  mutually recursive schemas do not eagerly instantiate child handlers.
  Resolve fixed inline layout through the selected Core struct handler.

### 4. Integrate build and installation

Add `bin_PROGRAMS = zflatc`, `zflatc_SOURCES = zflatc.cc`, and program-specific
linkage to `zi/src/Makefile.am`. Link the existing Z libraries; keep executable
link flags separate from library-only shared-object flags.

Add parser/emitter unit tests to `zi/test` and an end-to-end generation driver
to `zi/itest`, following the repository distinction for tests launching tools.
Extend the existing `zi/itest/Makefile.am`. Use Automake dependencies so fixture
export precedes generation, generation precedes compilation, and compilation
precedes comparison. Support parallel and out-of-tree builds, distribution,
and cleanup. Cross-compilation support is out of scope (resolved Q012);
do not require changes to the build system's existing cross-compilation behavior.

Document CLI, supported schema features, heap naming, generated API, and test
entry points in module documentation. Keep canonical `flatc` checks optional
under `zi/interop`.

### 5. Prove the saveFBS → zflatc → C++ round trip

Build a TAP fixture exporter declaring original native structs and `ZfStruct`
metadata, then call `ZiFB::saveFBS<Original>` to produce an actual `.fbs` file.
Run the newly built `zflatc`, compile and link both resulting files, and compare
the generated types with the original fixture contract. Use separate exporter
and generated-code checker executables so original/generated types retain
identical C++ and wire names without redefinitions or ODR violations. Have the
exporter write FBS, original BFBS, sample payloads, and a normalized manifest of
the recoverable field contract. Have the checker consume those artifacts,
compare its generated metadata/schema, and write reverse-direction payloads
for the original fixture verifier. Keep non-FlatBuffers metadata out of the
manifest. Use compile-time assertions in the checker for native field types,
container derivation, heap identities, and selected reference bases.

Use local aliases for `ZiFB::As<T>::Handler<T, Facet>` in generated-code test
consumers. For builder-based table output, call the selected handler's
`save<Filter>(builder, value)` and finish with the existing builder. Inline
saving uses the struct handler's `save(dst, value)`; union saving uses its
handler with the original union schema type retained. Materialize through
`ZiFB::handler(view)` or `ZiFB::handler<T, Facet>(bytes)`, and verify
external bytes explicitly. Keep borrowed view lifetimes and original builder,
storage, heap, and I/O pool choices. Do not generate codec implementations.

Add `zifbsave`, `zifbcheck`, and a TAP driver `zflatctest` to `zi/itest`, with
lowercase source basenames. Make generation rules depend on `zifbsave` and
`zi/src/zflatc`; compile `zifbcheck` against both generated files and link it
against the normal Z dependencies. Add separate rule outputs for option
variants so parallel builds cannot overwrite each other's fixtures. Register
`zflatctest` with `tap_drivers`, and distribute fixture sources and schemas.
The driver orchestrates checks, reports TAP through `ZuTestUtil`, and cleans
its temporary artifacts. Build-time generated sources live in the build tree
and are listed in Automake cleanup rules.

Comparison must include:

- C++ member spellings, canonical scalar types, named derived container types,
  element types, heap identities, and materialized field values. Exact C++
  type identity is not expected between separately generated declarations.
- ZfStruct field count/order/IDs, field typecodes, representable properties,
  defaults, enum mappings, active/deprecated slots, and ZiFB wire names/IDs.
  Inspect the selected handler's `SchemaFields`, `AllFields`, and table
  `id<F>()`; use the Core struct handler's layout for inline offsets/size/
  alignment. Read wire schema names from `As<T>` and field names from the
  retained FB properties. Do not equate active-field traversal with complete
  schema traversal: deprecated slots and enum/flags dependencies still belong
  to the schema contract.
  Metadata fidelity covers FlatBuffers semantics only; recovery of non-FBS
  ZfStruct metadata is out of scope (resolved Q008). Generated canonical types
  and heap IDs remain independently required and tested.
- Normalized reflected schema contracts, including inline layout/alignment,
  unions and root identity. Compare semantic schema data rather than relying
  solely on generated text, BFBS byte equality, or native `sizeof` equality.
- Payloads written by original types and verified/read/materialized by
  generated types, then the reverse direction. Check string/array ownership
  after releasing the source bytes and verify nested ownership in both pointer
  modes. Fix demonstrated materialization gaps required by the supported
  emitted schemas rather than weakening this check.

Fixtures cover snake/camel names, explicit wire-name overrides, all supported
scalars and default boundaries, empty/nonempty strings and arrays, embedded
NUL data, enums/flags, nested tables, inline structs/arrays, recursive schemas,
unions, deprecated fields, required members, alignment, canonical Z values,
and facet-specific schema export. For facet-specific exports, compare the
exported schema contract against the generated canonical `FB` facet, selecting
`ZuFacet::FB` explicitly in codec/schema comparisons where needed. Do not
reconstruct original facet declarations or compare non-FlatBuffers metadata
(resolved Q008/Q009).

Repeat generation and compilation using `--alt`, checking source/header
includes and the absence of accidentally required `.hh`/`.cc` companions.
Exercise default `ZuPtr` references and `--ref` with each of `ZuObject`,
`ZuPolymorph`, `ZmObject`, and `ZmPolymorph`, including a `--ref`/`--alt`
combination. Check pointer member/element types, base inheritance, null and
nested materialization, reference counts, and destruction without leaks.
Compare ref-counted objects through references rather than requiring copies;
do not assume recursive schemas require cyclic owning runtime graphs.
Reuse `NestedLoadTest` as existing codec regression coverage; add generated-code
checks for these same contracts rather than treating pointer materialization
and facet propagation as unimplemented codec work. Keep object-vector elements
nonnull and test absent pointers on optional fields. Use `All` explicitly in
save-only/read-only fixtures when fields lack constructor/key metadata.
Verify default outputs alongside an input in a subdirectory, `-o foo` outputs
in the current directory, output stems containing directories, options after
the input, and schema includes found through `-I`, in both extension modes.
Use repeated `-I` options with conflicting include basenames to verify that
the first matching directory wins; reverse the options and check that the
selected include changes accordingly.
For `foo.fbs` including `bar.fbs`, check that generating `foo` emits the
dependency header include but creates no `bar` outputs. Generate `bar`
separately, then compile/link the combined result without duplicate declarations;
repeat with `--alt` and check `"bar.hpp"`.
Compile/link generated output with no export macro and with `--export ZiAPI`,
including multiple translation units, and check decoration of generated types
and metadata against the established Z macro conventions.
Add negative cases for malformed syntax, missing/cyclic includes, unresolved
symbols, duplicate IDs/names, naming collisions, unsupported attributes/types,
invalid defaults, and unwritable output. Check useful diagnostic locations.
Exercise overwriting an existing pair, preservation of that pair after invalid
input or staging failure, and cleanup/rollback on a handled replacement failure.

### 6. Validate and accept

Retain the post-refactor build configuration and existing artifacts. Batch
implementation changes, then incrementally build owning sources before tests
with `-j8`. Limit builds and tests to `zi`; do not rebuild or retest dependent
modules outside `zi`. Run existing ZiFB tests, new unit tests, and generation integration
tests through their `make test` targets. Do not run configure, `z.config`, or
`make clean`, switch compilers/platforms, or introduce a separate sanitizer or
benchmark build as part of this work.

Verify parallel generation and generated-file cleanup using isolated fixture
outputs; keep rules compatible with out-of-tree builds without requiring a new
configured build. Verify install rules and generated-code dependencies against
the retained configuration. Run the final Valgrind memory/leak checks outside
the sandbox with full networking access through `libtool exec`, never `.libs`
executables directly. Record unavailable checks
explicitly. Any necessary codec changes must preserve the completed refactor's
ownership, wire behavior, storage tuning, and performance; do focused regression
checks for those changes without reopening its consolidation work.

Completion requires both extension modes, default and reference-counted object
references, executable-generated compiled fixtures, semantic metadata
comparisons, bidirectional codec checks, and
verified member heap IDs. Existing ZiFB behavior and tests must continue to
pass. Optional canonical `flatc` interoperability is additional evidence.

## Implementation checkpoint (2026-10-11)

The working tree now contains the executable, build/install rules, parser and
emitter unit tests, compiled integration fixtures, representative expected
declarations, and module documentation. Implemented checks include:

- CLI output paths, both extension modes, ordered repeated `-I`, separate
  dependency generation, export decoration and multi-translation-unit linkage.
- Native ownership, member/inner-container heap IDs, all four reference bases,
  recursive pointers, semantic BFBS/metadata comparisons and bidirectional
  payloads. Union payloads exercise a table pointer, inline struct with a fixed
  array, canonical `int128_t` recovered from `Zf.Int128`, and absence.
- Source `Wire` facet exports with renamed root, child and inline fields,
  compared through generated canonical FB metadata and loaded in both
  directions. The source fixture explicitly renders its nested native types.
- Canonical `Zf.Bytes` wrapper-table fields use an identified owning byte array
  and the retained `BytesMap`; direct byte vectors retain their distinct wire
  representation. Canonical scalar values in union alternatives now use their
  existing ZiFB mappings through `UDT`.
- Inline layout validation, including nested array extents, empty structs and
  forced alignment below natural alignment; enum width/flag-bit bounds and
  union discriminator bounds; short-buffer stability and naming edge cases.
- Staged replacement, preservation on invalid input/staging failure, injected
  second-replacement rollback and temporary-file cleanup.

Current validation passed `zi/test`'s `make test` (26 TAP drivers, 163 top-level
tests) and `zi/itest`'s `make test` (5 TAP drivers, 39 top-level tests, plus its
Python/shell/Go residue checks). Parallel isolated generation and compilation
of both included-schema extension modes passed using the retained Makefile and
configuration; isolated `clean-local` removed generated directories. Generated
test headers use build-directory include lookup, so source-tree artifacts do
not satisfy the out-of-tree compilation accidentally. An isolated staged
installation of `zflatc` succeeded and its `--help` ran with already-built Z
dependencies staged for dynamic linking. No configure, retained-tree clean, or
dependent build/test outside `zi` was performed.

Unrestricted `libtool exec valgrind` runs passed for the generator, parser unit
tests, all seven generated consumers, original forward/reverse verifiers for
all union alternatives under Core/Wire, both included-schema consumers, and
`ZiFBTest`: zero errors and zero definitely, indirectly or possibly lost bytes
in all 20 logs. These results prove the covered fixtures, not complete schema
coverage.

### Coverage checkpoint (2026-10-12)

The generated round-trip fixtures now cover all canonical integer/value vector
element mappings, both floating widths, bitmap values, IPv4/IPv6 and absent IP
values, finite mutually recursive table graphs, and repeated loads that replace
owning pointers and clear absent pointers. Compile-time checks verify native
element types, owning array bases, and member heap IDs. `Zi.IPv4`, `Zi.IPv6`,
and `Zi.IP` schemas recover the existing canonical native types after exact
shape validation.

Byte-array union alternatives now use identified owning `ZuDerive` wrappers.
A union containing bytes takes a compile-time heap-ID parameter; each generated
member supplies its own qualified containing-type/member ID. Two fields using
the same union check distinct heaps and independent payload ownership after
source-buffer release. Reflection coalesces union instantiations only when
their wire names, alternative names/order, and canonical payload types match;
incompatible duplicate names still fail. Table, inline, 128-bit, bytes, and NONE
alternatives round-trip in both directions.

Built-in validation now runs after field attributes have been resolved, so
incompatible required, deprecated, and force-aligned built-in fields are
rejected. Deprecated named-container metadata qualifies the owning type's
container alias. Buffer writes pass `bytes` directly.

Validation with the retained configuration: `make -C zi/src -j8`,
`make -C zi/test -j8`, and `make -C zi/itest -j8` passed without warnings.
`prove ./ZiFlatcTest ./ZiFBTest ./ZiFBReflectTest ./ZiFBIOTest` passed
(4 drivers, 42 top-level tests). `prove ./zflatctest` passed, including all seven
generated consumers, both include consumers, CLI cases, declaration goldens,
and forward/reverse Core/Wire payloads. Valgrind on all seven updated consumers, parser tests, the generator, and
five original reverse-payload verifications reported zero errors and zero
definitely, indirectly, or possibly lost bytes in all 14 logs under
`/tmp/zflatc-valgrind/bytes`. These checks do not certify untested requirements. No build reconfiguration or
rebuild/retest of dependents outside `zi` was performed.

### Built-in root checkpoint (2026-10-12)

Generated `Zf.Bytes` roots now derive from identified owning byte arrays with
heap ID `"Zf.Bytes.data"`; `Zf.Bitmap` roots derive from canonical `ZtBitmap`.
Both use the concrete-type heap and selected reference base. `AsMapped<Map>`
retains the existing canonical table descriptor, verification, and saving, and
materializes native containers through the retained value conversion. It
preserves the selected facet. Canonical library bytes/bitmap metadata now
provides matching Core and FB facets with implementations in `ZiFB.cc`.
Generated sources do not duplicate built-in wire declarations or metadata
implementations.

The exporter emits root schemas through `saveFBS` on the existing canonical
wire types. Six compiled `zifbroot` variants cover default ownership, both
extension modes, and all four reference bases. They compare normalized BFBS
and recoverable metadata, load original payloads with `ctor()`/`alloc()`/`load()`,
release source storage, and verify reverse payloads through the original wire
codec. Reverse output uses a separate builder after the original buffer has
been released; Valgrind caught and verified correction of fixture reuse of a
builder whose backing storage had been freed.

Both module test targets pass: `zi/test` has 26 TAP drivers/163 top-level tests;
`zi/itest` has 5 TAP drivers/39 top-level tests plus Python/shell/Go checks. Its
`zflatctest` driver now also runs all six root consumers. Unrestricted
`libtool exec valgrind` reports zero errors and zero definitely, indirectly, or
possibly lost bytes for all six root consumers; logs and status are under
`/tmp/zflatc-valgrind/roots`. The retained configuration and artifacts were
used; no configure, `z.config`, retained-tree clean, or dependent module
build/test was performed.

### Final acceptance (2026-10-12)

The implementation and required acceptance gates are complete. The inventory
in `zi/README.md` was checked against `ZfFieldTC`, ZiFB value/field mappings,
and the constructs emitted by `ZiFB::FBS`. The final audit closed the following
gaps without expanding the agreed schema scope:

- Built-in roots declared by dependencies receive owning adapters; guards by
  canonical wire name prevent duplicate adapters through indirect includes.
  Both extension modes compile separate dependency/root/include-chain outputs.
- Reference bases normalize qualified names, including leading `::`, before
  selecting includes and emitting globally qualified inheritance. A compiled
  consumer-provided `flatcref::Base` complements the four standard bases.
- Reject schema identifiers using reserved framework prefixes (`Z` followed
  by a lowercase letter), except recognized canonical built-in declarations
  with their exact validated wire shapes. Do not change framework macros to
  accommodate schemas declaring framework names. Negative parser tests cover
  the reserved namespace, type and enum cases.
- Deprecated containers retain their qualified native types. When a public
  constructor name hides an inherited container alias, metadata and fixed-array
  extent specializations name that alias through its implementation scope.
  Compiled `Axes::axes` and deprecated `Retired::retired` cases cover this.
- Generated inline assignment qualifies destination members with `this->`.
  The exported inline fixture's `v` member exercises assignment through all
  reference bases without hiding it behind the source parameter.
- Enum/flag helper names, `ZuDerive`'s reserved native type name `Base`, live
  members hiding their containing type, and names hiding the intrusive base
  interface under `--ref` are diagnosed before output publication (Q016).
  Default `ZuPtr` generation still permits reference-interface member names.

| Requirement | Acceptance evidence |
| --- | --- |
| Canonical field types, scalar widths/defaults and built-in values | `ZiFlatcTest`, `zifbsave`, `zifbcheck`, `zflatcvec.hh`; inventory in `zi/README.md` |
| Owning strings/arrays, nested container heaps, fixed extents | Generated-consumer compile-time type/heap/extent assertions; source-buffer-release checks |
| Tables, recursion, enums/flags, unions, alignment and field properties | Parser rejection cases, semantic BFBS/metadata manifests, forward/reverse payloads |
| Default pointers and four standard/custom reference bases | Eight compiled `zifbcheck` consumers, including the source `Wire` facet variant |
| Built-in bytes/bitmap roots | Six compiled `zifbroot` consumers; native factories/load, canonical metadata and reverse verification |
| Canonical FB facet and source-facet exports | Explicit FB comparisons and Core/Wire bidirectional checks; existing nested-load regressions |
| Both extensions, CLI paths, ordered `-I`, export and multiple translation units | `zflatctest` and separately compiled generated sources/consumer units |
| Separate dependency generation and native name lookup | Both `zifbinclude` variants, include-chain built-in roots and checked-in declaration goldens |
| Output preservation and handled replacement rollback | CLI failures plus injected second-replacement failure in `ZiFlatcTest` |
| Parallel/out-of-tree rules and generated cleanup | Isolated retained-Makefile builds of two root variants and both include variants; generated files/directories removed, sentinel retained |
| Installation | Actual staged `install-binPROGRAMS`; installed executable passes help and generation with `--ref ::ZuObject` |

Final incremental `-j8` builds of `zi/src`, `zi/test`, and `zi/itest` passed.
`make -C zi/test test` passed all 26 TAP drivers/163 top-level tests;
`make -C zi/itest test` passed all 5 TAP drivers/39 top-level tests and the
Python/shell/Go checks. The isolated four-consumer build/test and cleanup checks
also passed. Logs are `/tmp/zflatc-accept-{src,unit-build,unit-prove,itest-build,itest-prove}.log`
and `/tmp/zflatc-final-isolated-{build,prove,clean}.log`.
Staged installation passed with the retained configuration's unused `-pg`
link warnings and libtool's expected relink/staging notices; the installed
tool ran successfully. See `/tmp/zflatc-accept-install*.log`.

Final unrestricted `libtool exec valgrind` checks passed for the generator,
parser unit tests, `ZiFBTest`, all eight generated consumers, both include
consumers, all six root consumers, and ten original Core/Wire reverse-payload
verifiers: zero errors and zero definitely, indirectly, or possibly lost bytes
in all 29 runs. Logs and the checked status manifest are under
`/tmp/zflatc-valgrind/accept`.

The retained compiler/configuration and prebuilt dependencies were used.
No configure, `z.config`, retained-tree clean, compiler switch, or dependent
build/test outside `zi` was performed. Optional canonical
`flatc` interoperability was not rerun; cross-compilation remains out of scope.

## Corrective namespace audit (2026-10-12)

Restore `zu/src` and `zt/src` to `HEAD`, and remove the corresponding
`ZfStruct.hh` macro qualifications. The artificial schemas declaring framework
names were not a reason to change those libraries. Reject reserved framework
prefixes in schema declarations, preserving exact canonical built-in shapes
and framework symbols supplied through `--ref`/`--export`. Remove those fixture
declarations and unnecessary framework qualifications from generated code;
regenerate the dependency/root declaration goldens. Keep application type and
selected reference-base qualifications where they are needed for C++ lookup.

Audit both staged and pending code for the same scope error. The remaining
framework/dependent changes implement the requested ZiFB handler refactor,
facet/pointer loading corrections and documented I/O fixes. No further
framework macro changes for schema-name shadowing remain. Unstage all files
at the user's request; retain the corrected worktree and new files.

Incremental `zi/src`, the four focused unit drivers and `zi/itest` build with
the retained configuration and restored headers. The four unit drivers pass
42 top-level tests. `make -C zi/itest test` passes all five TAP drivers/39
top-level tests, nine Python tests and the shell/Go checks.
Unrestricted `libtool exec valgrind` passes for the parser,
generator and both dependency consumers, with zero errors and zero definitely,
indirectly or possibly lost bytes. Logs are `/tmp/zflatc-audit-*.log` and
`/tmp/zflatc-valgrind/audit`. No reconfiguration or build/test outside `zi`.

## Open questions and ambiguities (permanent IDs)

Never renumber, reuse, or delete a question ID. Append new questions with the
next unused number. Resolve entries in place with the decision and rationale;
retain resolved entries. Entries are open unless explicitly marked resolved.
Q001–Q016 are resolved; there are currently no open questions. Append any new
ambiguity as Q017 or later without reopening settled choices implicitly.

- **Q001 — Parser dependency and scope. Resolved.** Use a self-contained Z
  parser with no new production FlatBuffers dependency. Accept all `saveFBS`
  output. FlatBuffers schema features that Z cannot emit are out of scope;
  diagnose such input rather than extending Z to support it. This keeps the
  tool aligned with Z's schema emission and the required round trip.
- **Q002 — Heap-ID `<name>`. Resolved.** Use the qualified C++ containing
  type, expressed with dots, plus the camel-case C++ member name. This
  distinguishes equal member names in different types and is independent of
  the input filename and snake-case wire spelling. No heap-ID override syntax
  is specified by this decision; any proposed override requires a new question.
- **Q003 — Nested container heaps. Resolved.** Outer and inner owning
  string/array storage share the member's heap ID, with no suffix. Use Q002's
  qualified C++ type and camel-case member naming at every nesting level.
- **Q004 — Nested object ownership. Resolved.** Use `ZuPtr<T>` by default.
  Add `--ref <type>` to switch object references, including table-vector
  elements, to `ZmRef<T>` and derive generated C++ structs from the specified
  reference-counting base. The type will normally be `ZuObject`, `ZuPolymorph`,
  `ZmObject`, or `ZmPolymorph`. Preserve owning materialization and the selected
  policy through nested references; verify codec support and destruction for
  both modes.
- **Q005 — Naming edge cases. Resolved.** Require the FlatBuffers snake-case
  convention for field names. Reject keywords, reserved/overlong names, and
  snake-to-camel collisions rather than escaping or renaming them. Validate
  both schema names and generated C++ names, using the authoritative naming
  limit, and diagnose failures with the original wire name and source location.
- **Q006 — Native Z type recovery. Resolved.** Recognize ZiFB's built-in value
  schemas by qualified wire name and validate their structure. Emit their
  corresponding canonical Z types; reject recognized names with incompatible
  definitions. Generate ordinary structs for other schema types. Arbitrary
  application transforms cannot be reconstructed from their FBS wire
  representation alone; their recovery is out of scope, with no custom
  attributes introduced for that purpose.
- **Q007 — Feature coverage. Resolved.** Use the existing canonical C++ types
  for every `ZfFieldTC`. Derive the supported feature matrix from existing Z
  schema emission and implement those mappings, including fixed arrays,
  enums/unions, defaults, attributes, and alignment. Reject features Z cannot
  emit. Any feature Z emits that cannot currently be reconstructed is an
  implementation gap to identify and address within this scope, not a reason
  to invent replacement types or expand support to the full FBS language.
- **Q008 — Metadata round-trip fidelity. Resolved.** Non-FlatBuffers metadata
  round-trip fidelity is out of scope. Compare metadata that expresses the
  FlatBuffers schema contract; do not add Z-specific attributes to preserve
  arbitrary ZfStruct properties, constructor policies, original heap IDs,
  transforms, or original borrowed/owning C++ types. Independently verify that
  generated code uses the required canonical C++ types, ownership policy, and
  default heap IDs; these are generation requirements, not recovered metadata.
- **Q009 — Facet fidelity. Resolved.** Reconstructing source facets is not
  required. Generated code uses the default canonical `FB` facet declared in
  `ZiFB.hh` (`ZuFacet::FB`), rather than recreating the source's selected facet.
  Round-trip tests compare the exported FlatBuffers contract through that
  generated facet, selecting it explicitly where existing APIs require it.
- **Q010 — CLI/output policy. Resolved.** Without `-o`, emit alongside the
  input schema using its basename: `zflatc fbs/foo.fbs` generates `fbs/foo.hh`
  and `fbs/foo.cc`. `-o <stem>` overrides the output stem, not just its
  directory: `zflatc fbs/foo.fbs -o foo` generates `foo.hh` and `foo.cc` in the
  current directory. Relative output stems resolve from the current directory;
  current-directory inputs therefore also output there by default. Support
  repeated `-I <dir>` options for includes within schema files, searching
  directories in the order of the `-I` options. Support options after the input.
  `--alt` applies the same path rules with `.hpp`/`.cpp` extensions.
- **Q011 — Source/header split and exports. Resolved.** Add
  `--export <macro>`, for example `--export ZiAPI`; the default is no export
  macro. Apply the selected macro using established generated declaration and
  `ZfStruct`/`ZtEnum` export conventions. Use their declaration macros in the
  header and corresponding implementation macros in the source; prove
  multi-translation-unit linkage without adding unnecessary convenience APIs.
- **Q012 — Cross-compilation. Resolved: out of scope.** The current build
  system may or may not support cross-compilation. This work introduces no
  requirement to investigate, implement, or change that support, including
  build-machine tool overrides or cross-build generation rules.
- **Q013 — Included-schema emission and file replacement. Resolved for
  dependency emission.** Do not generate dependencies. Generated headers
  include their corresponding C++ headers: `foo.fbs` including `bar.fbs`
  produces `foo.hh` containing `#include "bar.hh"`. With `--alt`, use
  `#include "bar.hpp"` in `foo.hpp`. Parse dependencies for schema resolution,
  but generate their declarations/files only through separate invocations.
  File replacement is resolved separately under Q014.
- **Q014 — File replacement. Resolved.** Overwrite existing outputs by
  default. Validate the schema before replacing outputs and avoid leaving a
  successful-looking half-generated pair on failure. Stage both generated
  files before replacement and handle output/replacement errors accordingly.
- **Q015 — Generated helper names. Resolved implementation detail.** Reserve
  `Zflatc_` for generated implementation namespaces and reject that schema
  identifier. Scope helpers within it by public object name; use concise
  internal names distinct from container aliases and `ZuDerive_`'s `Base`.
  This prevents collisions between separately generated dependencies and with
  ordinary public names such as `O0`, `H0`, and members `heap`, `e0`, or `base`,
  while preserving the 28-byte identifier limit.
- **Q016 — Declaration/base name collisions. Resolved implementation detail
  under Q005.** Reject enum names reserved by generated `ZtEnum`/`ZtFlags`
  declarations (`N`, `T`, `Map`, `Map_`, `name`, `lookup`, `ZiFB_Fmt`) and flag
  names whose generated ordinal would be reserved or exceed 28 bytes. Reserve
  native type name `Base` for `ZuDerive`, and reject live members with their
  containing type's name. With `--ref`, also reserve `ref`, `deref`, `mvref`,
  `refCount`, and `withRef` for the intrusive interface, for native type names
  and live members; default `ZuPtr` members retain these spellings. Diagnose
  these cases rather than renaming schema/public identifiers. Reserve framework
  prefixes (`Z` followed by a lowercase letter) in schema namespace components,
  types and enum/union alternatives; permit only recognized canonical built-in
  declarations with exact validated wire shapes. Framework symbols remain
  usable in `--ref` and `--export`. Use implementation scope for hidden container
  aliases so containing-type/container coincidences remain usable. Do not
  modify framework declaration macros to accommodate reserved schema names.
  Heap IDs and recovered wire names do not change.

### Facet-specific size prefixing

ZiFB configuration follows the ZfURI NTP/ADL pattern: `ZiFB_Config(Facet *)`
is resolved through `ZiFB::Config<Facet>`, with `ZiFB_SizePrefixed<>` enabling
size prefixing for the selected facet (default false). Generated types continue
to declare their canonical FB field list. Typed codec call sites pass the facet
without a positional framing boolean; applications configure a separate facet
and reflect the FB field list when they need prefixed payloads. Builder callers
still finish their own arenas explicitly.
