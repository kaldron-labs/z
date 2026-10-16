# Complete `ZfCBOR`

## Decision

Implement RFC 8949 CBOR as a span-bounded, zero-copy binary codec modeled primarily
on `ZfASN1`, not on the internal `ZfJSON` tree.

The implementation has three layers:

1. one non-allocating scanner that validates and skips encoded items;
2. `ZfStruct` handlers that index recognized object fields into scratch spans and
   load values directly; and
3. direct encoding to the caller's output sink.

Do not add a dynamic tree.  `ZfJSON::AnyNode` exists because JSON parsing
externalizes an intermediate artifact for unresolved discriminated unions.
CBOR is self-delimiting, so the equivalent operation is the established Ztls
pattern: load a small discriminator projection, switch, then load the selected
schema from the same input span.  Reparse is the deliberate cost of omitting a
dynamic union/tree; do not add a shared generic index as a third design.

Scope is RFC 8949.  COSE, CDDL, diagnostic notation, registered application-tag
semantics, and RFC 8742 sequences are separate facilities and are not part of
this implementation.  A caller can process concatenated CBOR items by advancing
the offset returned by `scan()`; no sequence API is needed.

In particular, COSE is a Ztls concern: COSE key schemas, integer labels,
algorithm/curve checks, public-point construction, and all cryptographic test
vectors live in `ztls`, as a `ZfCBOR` dependent.  `ZfCBOR` contains no
crypto-related type, constant, validation, or fixture; it only provides the
generic CBOR structural and reflected-data mechanisms that Ztls uses.

Normative reference:

- [RFC 8949: Concise Binary Object Representation](https://www.rfc-editor.org/rfc/rfc8949.html)

This plan follows the repository priorities of integrity, latency, minimal
copying, and minimal allocation in [General principles](GUIDELINES.md#general-principles).

Typed load followed by save must preserve every represented CBOR data-model
value and its semantic CBOR type, matching the fidelity expected of `ZfASN1`.
This is semantic fidelity, not retention of serialization spelling: preferred
argument widths, definite output containers, and preferred shortest-exact
float widths may produce different bytes for the same value.  Do not retain
original wire spans or add a dynamic tree merely to reproduce non-preferred
spellings.

The strongest fidelity requirement is CBOR -> `ZfStruct` -> CBOR.  For every
value representable by its destination type, saving the loaded field must
reproduce the same semantic CBOR type and value.  The documented `ZuTime` and
`ZuDateTime` range/precision clamps are explicit host-mapping exceptions; do
not introduce any other silent rounding or narrowing.

The inverse direction is also required, but with a different standard: every
valid in-memory `ZfStruct` value must have a sensible, interoperable CBOR
representation.  Prefer a standard CBOR type or semantic tag capable of
representing the Z value exactly.  If CBOR genuinely cannot express the full Z
domain, choose the representation that minimizes information loss, document
the lost dimension or precision, and test the resulting `ZfStruct` -> CBOR ->
`ZfStruct` bound.  Do not declare host values unsupported merely to avoid
making this mapping decision.

Align encoding policy with the BER/DER split in `ZfASN1`.  Loading is
variation-tolerant: accept every well-formed representation admitted by the
typed schema, including non-preferred widths and indefinite containers.  The
canonical `ZuFacet::CBOR` saver emits one RFC 8949 core deterministic encoding.
Do not add separate permissive/canonical scanners or reject otherwise valid
input merely because its spelling differs from saver output.

## Public API target

Keep only the useful non-callback part of the current API:

```cpp
namespace ZfCBOR {

namespace Error {
  enum { /* errors */ };
}

struct Result {
  unsigned offset;
  int      error;

  explicit operator bool() const;
};

Result scan(ZuBSpan);

}
```

`scan()` is the public structural validator and prefix-length query.
Typed access is through `handler<T, Facet>()`; there is no public untyped item
or traversal API.  Internal header decoding and value skipping are shared by
both paths.  Do not create parallel cursor, event, view, result, options, or
callback families.  Update all dependents directly, as required by
[General principles](GUIDELINES.md#general-principles) and
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

## Wire scanner

### Complete grammar

Extend the existing scanner to handle every RFC 8949 encoding:

- unsigned and negative integers with 0-, 1-, 2-, 4-, and 8-byte arguments;
- definite and indefinite byte strings and text strings;
- definite and indefinite arrays and maps;
- nested tags with the full `uint64_t` tag-number range;
- false, true, null, undefined, and every simple-value encoding;
- binary16, binary32, and binary64; and
- break, legal only inside the matching indefinite string, array, or map.

Reject reserved additional-information values 28 through 30, truncated
arguments or data, invalid break placement, odd indefinite maps, and wrong
indefinite-string chunk types.  In major type 7, accept simple values 0 through
255 (including unassigned values) in their legal short or one-byte form; only
the reserved additional-information encodings are syntax errors.  Do not
validate or convert UTF-8 in the CBOR scanner; text payload
interpretation belongs to `ZuUTF` and the destination type.  Accept non-minimal
integer, length, tag, simple, and floating encodings: Postel's Law is explicit
repository policy in [No dogma](GUIDELINES.md#no-dogma).  Emission need not
reproduce a non-preferred input spelling.

Additional information 31 denotes an indefinite form only for major types 2
through 5.  For major types 0, 1, 6, and 7 it is invalid; for type 7 it is the
break byte and is valid only when the immediately enclosing parse state expects
the terminator of an indefinite string, array, or map.  This is enforced by the
same `skip()` routine, rather than by a public break or cursor state.

`scan()` parses one item and returns the consumed offset.  It does not reject
trailing bytes because prefix scanning is useful and already represented by
`Result::offset`.  A caller requiring exactly one item checks
`result.offset == input.length()`.  This avoids `scanOne()`, `scanPrefix()`, and
`scanSeq()` variants.  `handler<T, Facet>()` likewise loads the first complete
item and ignores the remaining suffix, matching `ZfASN1::LoadContext` and its
use by Ztls key loaders.  Do not impose stricter whole-span consumption in the
Ztls COSE or Zum attestation migrations.

### Internal parse state

Decode each initial byte into a small internal header containing only the major
type, additional information, decoded argument, and offsets needed by the
current call.  Keep it file-local or private to the scanner.  It is not a
public value model and does not outlive the input operation.

Use `UINT64_MAX` as the internal indefinite length/count sentinel; add no
separate flag.  Interpret it as indefinite only in the string/container path
after additional information 31.  Major type and additional-information
context preserve a legitimate scalar unsigned integer or tag numbered
`UINT64_MAX`; a definite string/container declaration of that size cannot fit
the finite input span and fails remaining-span validation.

Use one shared internal `skip()`/item routine for public validation, object
field indexing, array/map traversal, tag handling, and projection reparse.  Do
not expose internal type enums, headers, cursors, spans, or callbacks merely to
test them; test through `scan()` and typed handlers.  Use namespace enums, not
`enum class`, where an enum is still useful, following
[Enums](GUIDELINES.md#enums).  Keep common non-template declarations outside
templates, following [Code structure](GUIDELINES.md#code-structure).

### Control and bounds

Align with `ZfASN1`: expose no `Limits` policy and maintain no depth, item, or
data counters.  The input `ZuBSpan` is the bound.  Each item consumes at least
one byte, and all definite or accumulated string data comes from that span, so
separate counters duplicate the input-length bound while adding hot-path
branches.  Application message-size policy belongs at the caller before codec
entry, as it does for ASN.1.

Indefinite-length wire forms remain bounded by the span and must contain their
syntactic `break`; they do not make storage or execution unbounded.  Keep
recursive descent without a dynamic frame container or per-item policy checks.
Accumulate indefinite-string lengths with checked arithmetic and validate every
declared length against remaining input before advancing or allocating.  Use
`switch` for major type, additional-information width, and simple values, and
use early returns to keep the parser flat, per
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).  This
also aligns with
[Storage and capacity](GUIDELINES.md#storage-and-capacity) and
[Liveness](GUIDELINES.md#liveness).

For a typed owning byte or text destination, decode an indefinite string in
two lightweight passes.  The sizing pass reads and validates only each chunk
header, adds its declared length with checked arithmetic, and advances over the
payload by length without scanning its bytes.  Allocate one exact-sized
`ZtScratch` buffer (stack with named heap fallback), then make one traversal
that coalesces the chunk payloads into it.  Construct/assign the owning
destination from that scratch span before the scratch lifetime ends.  Do not
reserve incrementally, append through reallocations, or retain codec-owned
contiguous storage.

A definite string, or an indefinite string containing at most one chunk, may
load into `ZuBSpan`/`ZuCSpan` by borrowing the contiguous payload.  A
multi-chunk indefinite string cannot be represented by a non-owning contiguous
span without giving the codec hidden ownership; for such a destination it is
incompatible and follows the normal `ZfASN1`-aligned field-default/null path.
Callers requiring all legal chunkings must use an owning destination type.
The structural scanner accepts every valid chunking regardless of the typed
destination.  This follows [Storage and lifetime](GUIDELINES.md#storage-and-lifetime)
and [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).

Keep errors compact:

```text
OK Syntax
```

`Syntax` covers malformed wire structure; do not create a separate error for
every malformed byte.  Typed handlers follow `ZfASN1` and expose no separate
error code.
`Result::offset` is the consumed length on success and is unspecified on error,
matching `ZfASN1`'s failure-oriented parsing rather than adding a diagnostic
offset contract.  Do not allocate diagnostic strings in the hot path; a
`switch`-based error-name formatter can be added only when a caller needs one.

### Floating point

Decode binary16/32/64 with `ZuPun`, `ZuFP`, and explicit unsigned-bit
operations.  Follow the raw IEEE processing style of
`Zdf::FloatDecoder`/`Zdf::Encoder<FloatDecoder>` in
`zdf/src/ZdfCompress.hh`: check the span before each read, manipulate integer
bits with `ZuIntrin` operations, and reconstruct the floating value only after
the encoded bits are complete.  The Zdf compression format itself is not a
CBOR dependency; do not copy its compression-specific lookup tables.  Keep raw
bits local only for decoding and classification; do not publish a generic
floating value wrapper or promise bit-preserving conversion.

Align typed behavior with `ZfASN1`: NaN is one semantic value.  Loading any
well-formed CBOR NaN produces `ZuFP<F>::nan()` for the destination floating
type.  Do not preserve or expose source sign, signalling state, or payload;
CBOR does not require those distinctions for this codec's data model.  Saving
any NaN emits the preferred canonical binary16 encoding `0xf97e00`.  The
structural scanner accepts every well-formed CBOR NaN encoding.  Test
representative alternate encodings for normalization and require exact
canonical output.

On every typed save, emit an interoperable, conformant RFC 8949 preferred
serialization: binary16 when it preserves the value exactly, otherwise
binary32 when exact, otherwise binary64.  Implement selection as constant-time
tests over the normalized exponent/mantissa returned by `ZuFP::decode()`, using
the same `ZuPun` and `ZuIntrin` idioms as Zdf.  Do not search, allocate, change
the floating environment, convert through decimal, or perform a second
encoding pass.  Capture the native sign bit with `ZuPun` before
`ZuFP::decode()`, which normalizes both zeros to `{0, 0}`, so negative zero is
preserved.  Write network-order bytes explicitly; never copy native byte order
to the sink.  Infinities use binary16.

Do not add fixed-width float output properties without a concrete protocol
that requires a non-preferred representation.  The default must not permit
unconditional binary64 or lossy narrowing.  Keep the representability checks
constant-time and allocation-free, following the `ZuFP`/Zdf precedent;
interoperability and conformance are acceptance requirements.  This follows
both the latency and integrity priorities in
[General principles](GUIDELINES.md#general-principles) and keeps the checks
flat per
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

## `ZfStruct` loading

### Metadata

Add `ZuStructFacet(CBOR)` and properties under `ZuFieldProp::CBOR`, paralleling
the established JSON and ASN.1 integrations in
[Metadata](GUIDELINES.md#metadata):

```text
ID<ZuString> IntID Tag<uint64_t> Type<uint8_t>
ElemType<uint8_t> Simple Optional<bool> Opt
```

Declare the marker directly in the codec facet:

```cpp
namespace ZuFieldProp::CBOR {
struct IntID { };
struct Simple { };
}
```

`ZfStruct`/`ZuStruct` field IDs remain strings.  By default, encode that ID as a
CBOR text key.  `IntID` marks an ID as a canonical decimal CBOR integer key:
for example, field ID `"-42"` loads from and saves as major type 1 value -42.
Assert a marked ID has no leading `+`, no leading zero other than `"0"`, no
negative zero or non-digit, and is within CBOR's -2^64..2^64-1 integer domain.
These are application schema programming errors, not a codec input-validation
policy.  This one marker replaces the redundant
`UIntID`/`NIntID` value properties.

`Type` and `ElemType` override scalar and array-element wire mappings.  `Simple`
is a CBOR-specific marker on `ZfFieldTC::UInt8`; it encodes the scalar, or every
element of a `UInt8Vec`, as a CBOR simple value rather than an unsigned integer.
`Tag<N>` declares one required explicit outer tag, directly paralleling the
single schema tag in `ZfASN1::Fmt`; it does not replace a type's intrinsic CBOR
semantic tag.  Thus `Tag<100>` on a decimal field encodes
`100(4([exponent, mantissa]))`, and on a date-time field encodes
`100(0(text))`.  Bignum tags 2/3 likewise remain inside the declared outer tag.
Assert `Simple` only on `UInt8`/`UInt8Vec`, after applying a `Type` or
`ElemType` override.  An incompatible combination is an application schema
programming error; do not silently choose one property.

Support only that one directly declared outer tag.  Do not add a variadic tag
list: `ZfASN1` has no corresponding direct metadata surface, and no current
dependent requires one.  A missing or different declared outer tag makes the
field incompatible/defaulted, and an undeclared unknown outer tag is not
silently stripped.  The structural scanner still accepts arbitrary nested
tags.  If a concrete future schema requires multiple semantic layers, express
them through statically nested typed schemas, not a dynamic tag container.
This follows [Metadata](GUIDELINES.md#metadata),
[Static polymorphism and constraints](GUIDELINES.md#static-polymorphism-and-constraints),
and [Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

A reflected object may freely mix default text and `IntID` fields.  The
application owns duplicate reflected-key mistakes: this codec adds no
compile-time key-set validation.  Text `"1"`, unsigned 1, and negative -1 are
distinct keys.  The marker affects only that field; do not impose one key type
on the containing map.

Use detector traits and existing `ZuFieldProp` patterns; do not use concepts or
`requires`, per [Static polymorphism and constraints](GUIDELINES.md#static-polymorphism-and-constraints).
Use `ZfCBOR_Fmt` ADL selection with `AsObject` and `AsArray`, matching
`ZfASN1`.  Do not add speculative formatter categories.

### Object loading

Implement `AsObject::Handler` around a `LoadContext<Props, Fields>` analogous
to `ZfASN1::LoadContext`:

```cpp
template <typename Props, typename Fields>
struct LoadContext {
  // indexes into a caller-owned scratch array
};
```

The public handler retains only the input span.  Each lifecycle operation
creates its field-span index with an exact-sized
`ZtScratch(FieldSpans, Fields::N, Fields::N)`, constructs or loads the object
while that scratch remains in scope, and then discards the index.  `ZtScratch`
uses safe stack allocation when the current stack permits and the array's
named `ZmVHeap` fallback otherwise; this avoids both an unbounded handler
object and a guessed fixed builtin capacity.  Zero-field schemas request no
storage.  Do not retain a `ZtScratch` allocation in a returned handler: its
stack backing is operation-local.  This follows
[Storage and lifetime](GUIDELINES.md#storage-and-lifetime) and
[Heap allocation](GUIDELINES.md#heap-allocation).

The input must outlive the handler and every borrowed `ZuBSpan`/`ZuCSpan`
produced from it, exactly as for `ZfASN1`.  Owning destination fields copy from
the input during the lifecycle operation; non-owning destination fields retain
the input lifetime requirement.  Do not add buffer ownership to the codec.

Validate the object envelope before indexing fields.  Accept both definite and
indefinite maps as representation variants of the same declared map type; save
only definite maps.  If the object properties contain `Tag<N>`, require exactly
that tag followed by a map.  Without `Tag<N>`, require an untagged map and do
not silently strip an unknown tag.  A non-map root or tag mismatch invalidates
the context and leaves all fields absent/defaulted.  This follows `ZfASN1`:
tolerate encoding variants of the declared type without coercing a different
semantic type into it.

The context makes one pass over the CBOR map.  For each key it:

1. matches text keys directly against unmarked field IDs with `ZuMatcher`; for
   an integer key, compares its major type and argument with the compile-time
   decoded `IntID` spelling, including the `-2^64` boundary, and matches only
   `IntID` fields;
2. stores the encoded value span for a recognized field;
3. overwrites that span on each duplicate recognized key, so the last value
   wins exactly as in `ZfJSON::AsObject`; and
4. accepts and structurally validates any other legitimate CBOR key type, then
   skips that unknown key's value with the shared scanner.

Mixed key types are normal CBOR and are never themselves a load failure.  The
compile-time decimal conversion is bounded to the CBOR integer domain and
allocates nothing.  An object schema recognizes only
its declared text/integer keys; arrays, maps, tags, byte strings, floats,
booleans, null, and simple values used as unknown keys remain valid input and
are skipped without allocation.

The field index has exactly `Fields::N` logical entries.  Initialize those
spans once to null; do not default-construct decoded values that will
immediately be overwritten, per
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).

After indexing, reuse the `ZfASN1` handler lifecycle:

```text
ctor() alloc() new_() load()
```

Use `ZuFieldProp::GetCtor`, `ZfFieldFilter`, `ZuUnroll`, and field defaults
exactly as existing codecs do.  Do not duplicate object construction logic.
As in `ZfASN1`, the handler has no `operator bool()`, `error()`, exception, or
result wrapper.  Missing, incompatible, and out-of-range field values load as
the field default or established null value.  If envelope validation or map
indexing encounters malformed structure, clear the entire recognized-field
index so a malformed prefix cannot yield a partially plausible object.  A
caller that requires an explicit structural verdict calls `scan()` and checks
its result; typed loading does not impose that validation as a mandatory first
pass.

Also follow `ZfASN1` mutation semantics: `load(O &)` assigns load fields in
schema order and is not transactional.  Do not preconstruct a complete
replacement, roll back prior field assignments, or add an `update()` lifecycle
that the precedent does not have.  A caller requiring replacement semantics
uses `ctor()` and then moves or assigns the completed object at its own
boundary.

### Arrays

Align with `ZfASN1::LoadVec`: scan the array once into a self-contained
`ZtArray` of encoded element spans, then expose lazy element conversion through
`ZuMArray` to the destination constructor.  This span index is not a dynamic
value tree: it retains neither decoded values nor per-node type state, and each
element is decoded directly from the original input only when requested.  The
random-access index avoids rescanning from the start when existing Z container
constructors use `operator[]`.

Give the CBOR `LoadVec` its own small builtin span array and a named
`ZtArrayHeapID` fallback, following the ownership and return-by-value structure
of `ZfASN1::LoadVec`.  Do not use operation-local `ZtScratch` for this retained
adapter storage: unlike the object field index, `LoadVec` must remain valid as
the returned handler and destination constructor access it.  `ZtScratch` is an
alloca-or-heap overlay; `ZtBuiltin` and the embedded array used by
`ZfASN1::LoadVec` are the corresponding retained builtin-storage designs.

Set `ZfCBOR::LoadVec::BuiltinSize = 8`, matching `ZfASN1::LoadVec`.  The explicit
working assumption is that mainstream CBOR arrays have a size distribution
similar enough to mainstream ASN.1 sequences that eight spans provide the same
useful small-vector tradeoff.  Larger arrays remain fully supported through
the named heap fallback; eight is an optimization threshold, not a protocol
limit.  Record that rationale beside the constant.  Revisit the threshold only
in a subsequent tuning effort if real workloads provide contrary evidence.
This follows
[Storage and capacity](GUIDELINES.md#storage-and-capacity) and
[Heap allocation](GUIDELINES.md#heap-allocation).

Match `ZfASN1` for bounded destinations: `ZfCBOR` passes the complete `LoadVec`
to the destination constructor, assignment operator, or `ZfStruct` field
setter and adds no codec-level exact-length rule.  `ZuArray<T, N>` therefore
uses its established semantics: retain all input elements up to capacity `N`,
truncate later elements, preserve the shorter input length, and do not fill
missing positions with defaults.  A native C array exposed through a lambda
field follows that field setter's declared behavior, exactly as in the
`ZfASN1Test` fixed-array example.  Do not add CBOR-only rejection, padding, or
parallel fixed-array metadata.  Tests must cover shorter, exact-capacity, and
over-capacity `ZuArray` input plus one schema-defined native-array setter.  This
follows [Framework fit](GUIDELINES.md#framework-fit) and
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).

Do not add `AsMap`.  `ZfASN1` has no corresponding handler, and no current
ZfCBOR dependent loads an arbitrary run-time map into a Z map.  The wire scanner
still accepts and validates all CBOR maps, including mixed or compound key
types.  Typed map loading is provided only by `AsObject` over reflected string
IDs, with `IntID` for integer wire keys.  Add a general map adapter only with a
  concrete dependent; do not pre-emptively introduce key unions,
container mutation semantics, or run-time sorting.

### Discriminated alternatives

Follow `Ztls::LoadPK` and `Ztls::LoadSK`:

```text
load small discriminator projection from input span
  -> switch on discriminator
  -> load selected complete schema from the same span
```

The second handler rescans the containing map.  This is the direct consequence
of choosing no `ZfJSON`-style dynamic union/tree and aligns with the existing
`Ztls::LoadPK`/`LoadSK` use of `ZfASN1`.  Do not design a transferable or generic
field-span index: it would add a third representation, couple projection and
alternative schemas, and recreate part of the intermediate-tree problem.

Do not add `ZfCBOR::Union`, `AnyNode`, a node heap, union-owned input, or an
unresolved union alternative.  Discriminated objects use projection and
selected reparse exactly as above.  If a future fragmented-input API cannot
keep its source buffer alive, solve buffer ownership at the I/O boundary with
the established pooled buffer facilities, not inside the CBOR value model. See
[Storage and lifetime](GUIDELINES.md#storage-and-lifetime) and
[I/O and system integration](GUIDELINES.md#io-and-system-integration).

### Default field mapping

Implement and test every `ZfFieldTC` scalar and vector.  Map every CBOR
data-model primitive or vocabulary value onto the corresponding existing field
code and Z primitive:

| CBOR value | `ZfFieldTC` | Z/C++ type and rule |
|---|---|---|
| unsigned integer | unsigned or signed integer field code | corresponding unsigned or signed type; range-check on load |
| negative integer | signed integer field code | corresponding signed type; range-check `-1-n` before conversion |
| byte string | `Bytes` | `ZuBSpan` or the declared owning byte container |
| text string | `String`/`CString` | `ZuCSpan` or declared string type; text interpretation remains with `ZuUTF` |
| array | matching `*Vec` or `UDT` with `AsArray` | destination vector/array type, loaded through the `LoadVec` span index |
| map | `UDT` with `AsObject` | reflected object with string IDs and optional `IntID` markers |
| explicit outer tag | underlying field code plus `CBOR::Tag<N>` | underlying Z value; the required tag is schema metadata, not a dynamic wrapper |
| `false`/`true` | `Bool` | `bool` |
| `null` | any nullable field code | that type's established `ZuCmp<T>::null()` or `ZuUnion<void, T>` empty alternative |
| `undefined` | `UInt8` plus `Simple` | `uint8_t{23}`; do not silently coerce it to null |
| other registered or unassigned simple value | `UInt8` plus `Simple` | its exact `uint8_t` simple code |
| binary16/32/64 | `Float` | declared floating type, with checked conversion and the stated NaN normalization |
| tag 2/3 bignum | `UInt128`/`Int128` | corresponding 128-bit value after range checking |
| tag 4 decimal fraction | `Fixed` | `ZuFixed`, preserving mantissa and decimal scale exactly |
| tag 4 decimal fraction | `Decimal` | `ZuDecimal`, normalized exactly into its 10^-18 internal scale |
| tag 1 epoch time | `Time` | `ZuTime`; integer seconds when integral, otherwise closest binary64 seconds |
| tag 0 RFC 3339 time | `DateTime` | `ZuDateTime`; RFC 3339 text on input, normalized UTC text with up to nine fractional digits on save |

Do not add `ZfFieldTC::Simple`: `ZfFieldTC` classifies host field types shared by
all codecs, while “simple” is a CBOR wire representation of an 8-bit vocabulary
code.  The existing `UInt8` code plus `ZuFieldProp::CBOR::Simple` is lossless
and keeps CBOR policy out of JSON, ASN.1, YAML, and configuration dispatch.
Values 20 through 23 also remain available through this raw mapping when a
schema needs their exact vocabulary code rather than their normal `Bool` or
null semantics.
The break byte is container syntax, not a data-model value, and has no field
mapping.  Accept every assigned or unassigned simple-value code 0 through 255;
the scanner rejects only the reserved major-type-7 additional-information
encodings, as specified above.

Keep absence, null, and undefined distinct.  A missing map field follows the
existing field default/`Optional` behavior.  A present CBOR null loads the
destination type's established null or empty-optional value.  Where a type's
null sentinel aliases a legitimate value, the schema must use
`ZuUnion<void, T>` to preserve both states.  CBOR undefined is accepted only by
an explicit `UInt8` plus `Simple` field and loads as 23; for an ordinary
nullable field it is incompatible and therefore loads that field's
default/null value, following `ZfASN1`.  Saving follows the same mapping, so no
CBOR-only dynamic union is required.

Map `ZuFixed` and `ZuDecimal` to the RFC 8949 tag 4 decimal-fraction form
`4([e, m])`, whose value is `m * 10^e`.  For `ZuFixed`, save
`e = -ndp` and `m = mantissa`; do not strip trailing zeroes because `ndp` is
part of the Z value and CBOR can preserve it.  For `ZuDecimal`, begin with
`e = -18` and `m = value`, then divide trailing decimal zeroes from `m` while
incrementing `e`; `ZuDecimal` has no separately retained scale to lose.  Encode
the exponent as a major-type integer and the mantissa as an ordinary integer or
tag 2/3 bignum as its range requires.  Null sentinels save as untagged CBOR
`null`, not as a floating NaN.

On fixed/decimal load, accept every valid tag 4 pair whose value can be
rescaled exactly into the destination range and precision.  Accept non-minimal
integer and bignum spellings under the existing variation-tolerant rules.
Checked multiplication or division by powers of ten must prove exactness before
conversion; when reducing excess fractional scale, require the discarded
decimal digits to be zero.  An `NDP` field constraint must likewise be satisfied
by exact rescaling, never rounding.  Do not accept untagged integers or binary
floats as fixed/decimal values by default because saving them as tag 4 would
change their CBOR semantic type.

Map `ZuTime` to RFC 8949 tag 1 epoch time, matching its native signed seconds
plus nanoseconds representation.  Save integral seconds as a major-type
integer.  When nanoseconds are nonzero, emit the closest binary64 seconds value
using round-to-nearest; tag 1 does not permit a decimal-fraction child, so this
is the unavoidable interoperable loss for arbitrary in-memory `ZuTime` values.
Document and test the error as a function of epoch magnitude.  Admit every
finite CBOR-originating tag 1 float; convert it through `ZuTime`'s native
nanosecond representation and clamp values outside that representation's
signed-seconds range to its nearest endpoint, as for `ZuDateTime`.  Do not
reject valid CBOR merely because its host representation is less precise.
Null saves as untagged `null`.

Map `ZuDateTime` to RFC 8949 tag 0 only.  Emit uppercase UTC RFC 3339 text with
a four-digit year and zero to nine fractional digits, trimming trailing
fractional zeroes.  Before saving, saturate a non-null value into the inclusive
range `0000-01-01T00:00:00Z` through
`9999-12-31T23:59:59.999999999Z`; values below or above that range encode as the
nearest endpoint.  This clamp is the explicit, tested loss on the
`ZuDateTime` -> CBOR -> `ZuDateTime` path and ensures that every saved
`ZuDateTime` is a conforming tag 0 value.  Do not fall back to tag 1.

For Postel-tolerant point-in-time input, `Time` and `DateTime` handlers also
accept the other standard time tag.  Treat tags 0 and 1 here as alternate
standard representations of the declared point-in-time field, analogous to
BER input variants; each saver still normalizes to its field's selected output
above, with the stated `ZuTime`/`ZuDateTime` host-range rules.  Validate tag 0
with the RFC 3339 grammar and delegate character/date conversion to the
existing `ZuDateTime` facilities.  Do not accept untagged strings or numbers
as implicit times.

These mappings use only RFC 8949 standard tags, add no generic tag-semantics
registry, and keep conversion in fixed-width checked arithmetic.  They follow
[Framework fit](GUIDELINES.md#framework-fit),
[No dogma](GUIDELINES.md#no-dogma), and
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).

Typed loads accept any valid integer width but range-check before narrowing.
They do not coerce float to integer, text to bytes, null to zero, undefined to
null, or an unknown tag to its child.  Optional/null handling follows existing
Z sentinel and `Optional` conventions; do not add parallel boolean state, per
[Framework fit](GUIDELINES.md#framework-fit).

Apply Postel's Law to tag 2/3 bignums as well.  On input, accept a bignum byte
string with redundant leading zero bytes and accept tag 2/3 values that could
have been represented by major type 0/1.  Strip leading zeroes logically while
accumulating, reject only a value outside the destination's signed or unsigned
range, and preserve the tag 3 rule `value = -1 - n` through checked unsigned
arithmetic before conversion.  Do not allocate merely to normalize the input
payload.

On output, emit the peer-compatible preferred representation: use major type
0/1 whenever the value fits its `uint64_t` argument domain; otherwise use tag
2/3 with the shortest non-empty big-endian magnitude payload and no leading
zero byte.  Thus permissive bignum spellings normalize on save, just as BER
input normalizes to DER output in `ZfASN1`.  Test redundant-zero input, small
values under both bignum tags, zero and -1, the major-type boundaries, both
128-bit limits, and one-byte overflow on each destination type.  This follows
[No dogma](GUIDELINES.md#no-dogma) and
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).

For text fields, expose or copy the CBOR payload according to the destination
string type.  Delegate any UTF validation or width conversion to `ZuUTF`; do
not duplicate Unicode tables or validation logic in `ZfCBOR`.  This follows
[Framework fit](GUIDELINES.md#framework-fit) and
[Strings](GUIDELINES.md#strings).

## Encoding

Write directly to the caller's `S &` sink using short shared functions for the
initial byte, argument, scalar, string, tag, and container header.  Integer,
length, and tag arguments use the shortest width.  Strings and byte spans write
their header and existing storage without a temporary.  Encode 128-bit values
outside the major-type range with tags 2/3 and a minimal big-endian payload.

Use exactly the `ZfASN1` sink contract: the sink supports
`operator <<(char)` for one encoded byte and `operator <<(ZuCSpan)` for an
existing contiguous byte run; `save(S &, const O &)` returns the same `S &` for
chaining.  Emit byte strings through a direct `ZuCSpan` view without copying.
Do not require `reserve()`, writable uninitialized tails, callbacks, or a
CBOR-specific sink trait.  Allocation policy and append failure semantics
belong to the sink implementation, as they do for `ZfASN1`; the codec adds no
parallel status channel or exception contract.  This follows
[Framework fit](GUIDELINES.md#framework-fit),
[Type and API friction](GUIDELINES.md#type-and-api-friction), and
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

Typed arrays expose their counts, so emit a definite header and values in one
pass.  For reflected objects, make one cheap compile-time unrolled pass to
count included fields, emit the map header, then emit fields.  This is not an
ASN.1-style value-digestion stash: CBOR needs only the field count.  Do not
allocate `SaveValue` records.

Emit maps in RFC 8949 core deterministic order: compare the lengths of the
keys' deterministic encodings first, then compare those encodings bytewise
lexicographically when their lengths match.  For reflected objects, define a
compile-time key-order trait and use `ZuTypeSort`, directly paralleling
`ZfASN1`'s compile-time `ASN1Order`; optional-field omission preserves the
remaining order and needs no run-time sort.  Declared schema order is not a
wire-order contract.

Since the codec exposes no `AsMap`, it performs no run-time key collection or
sorting.  Compile-time object ordering is allocation-free.

Typed save emits definite-length containers only; every CBOR data-model value
has such an encoding.  Do not add indefinite-output primitives or a stateful
streaming writer without a consumer.  The decoder must still accept all legal
indefinite forms.

CTAP2 canonical CBOR is a distinct deterministic profile: in particular, its
map ordering, tag restrictions, and treatment of float widths differ from RFC
8949 core deterministic encoding.  Current Ztls/Zum dependents decode CTAP2
data but do not emit it.  Aligning with `ZfASN1` BER input behavior, accept
non-canonical spellings there when the CBOR is otherwise well-formed and passes
the COSE/WebAuthn schema checks.  Duplicate keys are accepted and the last
recognized value wins.  Do not add a CTAP2 facet or a
second canonicality-validation pass without an emitting consumer or a hard
protocol requirement to reject input.

Expose the established surface only:

```cpp
ZfCBOR::save<Facet>(out, value);
auto h = ZfCBOR::handler<T, Facet>(input);
```

Do not add `saveUpd()` or `saveDel()`.  `ZfASN1` exposes neither, and current
ASN.1 and CBOR applications do not require codec-defined update or deletion
documents.  Which fields constitute an application update or deletion is an
application-schema concern; the application must construct the corresponding
message type and pass it to ordinary `save()`.  Continue honoring the
established compile-time `ZfFieldFilter::Save` and CBOR `Optional` behavior for
that message type.  This avoids importing JSON-specific patch semantics into a
binary codec and follows [Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

Keep non-template scanner and numeric primitives in `ZfCBOR.cc`; keep metadata
and handler templates in `ZfCBOR.hh`.  Do not split headers without a concrete
build or dependency problem.  This follows
[Code structure](GUIDELINES.md#code-structure) and avoids speculative API
surface.

## Audit against `GUIDELINES.md`

### Code and control flow

- Move all declarations that do not depend on template parameters out of
  handlers.  Cache span ends and lengths outside loops.  Use a cached byte when
  branching repeatedly on it.  These directly address
  [Code structure](GUIDELINES.md#code-structure).
- Use `switch` for major type, width, simple value, field type, and
  discriminator.  Flatten errors with early returns.  Factor repeated scalar
  and container code through templates or short non-template primitives.  See
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).
- Parsing is necessarily linear in the input span.  Do not add redundant work
  counters, polling, blocking, locks, atomics, or scheduler interaction.  See
  [Liveness](GUIDELINES.md#liveness) and
  [Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies).

### Storage, allocation, and movement

- Scanner, validation, discriminator projection, and scalar conversion
  allocate nothing.  Object indexes use exact-sized `ZtScratch` span storage:
  stack when available, with the named heap fallback for large schemas or low
  remaining stack.
- Decode directly from input spans and encode directly to output.  Do not
  coalesce definite strings, create temporary contiguous values, or copy a
  complete item for inspection.  See
  [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).
- Indefinite strings use a header-only sizing pass, one exact `ZtScratch`
  allocation, and one coalescing copy before destination construction.
  Multi-chunk strings do not load into borrowed contiguous spans.
- If any unavoidable scratch or destination overflow allocation is introduced,
  use `ZtScratch`, `ZtBuiltin`, `ZmHeap`, or `ZmVHeap` with a specific heap ID,
  per [Heap allocation](GUIDELINES.md#heap-allocation) and
  [Storage and lifetime](GUIDELINES.md#storage-and-lifetime).
- Add no fixed lookup tables or guessed builtin capacities.  Object indexing
  uses exact schema-derived `ZtScratch` storage.  See
  [Storage and capacity](GUIDELINES.md#storage-and-capacity).
- Keep iterator scope local to each map traversal.  Count the compile-time
  `ZfFieldFilter::Save` fields that remain present after `Optional` omission
  before emitting the reflected-object map header.  See
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).
- No long-lived container, garbage collection, intrusive reference-counted
  value, cache-line-shared state, or separate object/container-node allocation
  belongs in this stateless codec.  These storage audit flags are inapplicable
  by construction; do not introduce such state while implementing it.  See
  [Storage and capacity](GUIDELINES.md#storage-and-capacity) and
  [Heap allocation](GUIDELINES.md#heap-allocation).

### Framework and type system

- Use `ZuBSpan`, `ZuSpan<char>`, `ZuUTF`, Z containers, `ZuMatcher`, `ZuUnroll`,
  `ZuSwitch`, `ZuBox`, `ZuFmt`, and `ZfStruct` facilities.  Do not use STL
  containers, iostream formatting utilities, or an external CBOR library.  See
  [Framework fit](GUIDELINES.md#framework-fit),
  [Compile-time data and matching](GUIDELINES.md#compile-time-data-and-matching),
  and [Formatting](GUIDELINES.md#formatting).
- Rely on Z span interoperability.  Use casts only for actual bit or character
  representation changes; do not cast among equivalent spans.  See
  [Type and API friction](GUIDELINES.md#type-and-api-friction).
- Use CRTP/static handlers, SFINAE detector traits, ADL format tags, and
  side-effect-safe defaults.  Add no virtual dispatch.  See
  [Static polymorphism and constraints](GUIDELINES.md#static-polymorphism-and-constraints)
  and [ADL tagging](GUIDELINES.md#adl-tagging).
- Use plain namespace enums and native `int`/`unsigned` locals, reserving
  fixed-width integers for wire values and compact stored metadata.  See
  [Primitive integer types](GUIDELINES.md#primitive-integer-types) and
  [Enums](GUIDELINES.md#enums).

### Naming and layout

- Keep the established names `Result`, `scan`, `save`, `handler`,
  `LoadContext`, `AsObject`, and `AsArray`.
- Use `UInt`, `NInt`, `Bytes`, `Text`, `IntID`, and `ElemType` rather than
  expanded names.  Every proposed identifier is below the 28-byte hard limit in
  [Length and abbreviations](GUIDELINES.md#length-and-abbreviations).
- Use camel case, reserve `m_` for private class data, use overloaded accessors,
  and reserve trailing `_` for internal implementations, per
  [Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes),
  [Accessors](GUIDELINES.md#accessors), and
  [Trailing underscores](GUIDELINES.md#trailing-underscores).
- Preserve the library header skeleton and local declaration alignment.  Use
  hard tabs with `ts=8`, `sw=2`; structs remain all-public and classes
  all-private.  See [Header layout](GUIDELINES.md#header-layout),
  [Indentation](GUIDELINES.md#indentation), and
  [Class and struct shape](GUIDELINES.md#class-and-struct-shape).

### Scope and cleanup

- Delete superseded narrow scanner logic as it is replaced; do not retain
  compatibility wrappers.  Update Ztls and Zum in the same change.  See
  [Scope and cleanup](GUIDELINES.md#scope-and-cleanup).
- Do not add logging, telemetry, background work, caches, global registries,
  or shared mutable state.  They are unrelated to a codec and would introduce
  allocation, lifetime, and concurrency concerns.
- Do not add logger callbacks, scheduler calls, blocking, polling, timers,
  manual hash functions, or iterators whose lifetime escapes a traversal.  The
  corresponding audit flags have no valid codec use case.
- Keep application-tag interpretation in applications.  The codec preserves
  unknown tags but does not dispatch through an IANA registry.

## Dependent migration and superseded-code deletion gate

The implementation is not complete when the new codec merely coexists with
the current scanner.  Replace the current implementation in place, migrate
every dependent in the same change, and delete all superseded parsing code
from those source files.  "Deletion" here means editing each dependent source,
header, test, and build file so its old `ZfCBOR` use is removed and replaced by
the completed `ZfCBOR` API.  It does not mean deleting the dependent files.
Retain each dependent source file unless it consisted entirely of superseded
`ZfCBOR` use and has no remaining purpose after migration; in that unlikely
case, delete the dead file and remove its build references.
Do not retain a legacy header, namespace, overload, adapter, feature flag, or
compatibility alias.  This is required by
[General principles](GUIDELINES.md#general-principles) and
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

Treat replacement of the installed, exported `ZfCBOR::scan()` contract as an
intentional source and ABI break.  Rebuild all dependents in the atomic
migration and add no compatibility symbol or adapter.  Do not change the
repository-wide `VERSION` as part of this codec implementation: release
versioning and packaging are separate repository release decisions.

The current dependency inventory is:

| Area | Files | Required disposition |
|---|---|---|
| Zf codec | `zf/src/ZfCBOR.hh`, `zf/src/ZfCBOR.cc` | Replace the narrow implementation in place. Delete `Limits`, all stored limit state, all depth/item/data counters, their branches, and the limit parameter to `scan()`. Preserve a name or signature only when it remains the simplest completed API, not as compatibility scaffolding. |
| Zf build | `zf/src/Makefile.am` | Continue installing/building only the completed implementation; add no legacy or parallel source. |
| Zf tests | `zf/test/ZfCBORTest.cc`, `zf/test/Makefile.am` | Delete limit fixtures and expectations; replace narrow scanner expectations with the complete wire and typed-codec suite. Do not leave a separate legacy test binary. |
| Ztls COSE | `ztls/src/ZtlsCOSE.hh`, `ztls/src/ZtlsCOSE.cc` | Add the COSE equivalent of `ZtlsPK`: reflected integer-label key schemas, `LoadPK` and `LoadSK` discriminator/reparse loaders, and direct save overloads for every key family supported by both `ZtlsPK` and COSE (RSA, P-256 EC2, and Ed25519 OKP). Keep COSE and ES256 key/signature conversion out of `ZtlsSec`. Generic key loaders accept and skip the optional COSE algorithm label, and generic savers omit it. `ES256::loadPK()` is the WebAuthn profile adapter and checks `alg == -7`. |
| Ztls tests | `ztls/test/ZtlsSecTest.cc` | Retain ES256 positive/negative coverage and add public/private save/load round trips for RSA, P-256 EC2, and Ed25519 OKP. |
| Zum WebAuthn | `zum/src/ZumWebAuthn.hh`, `zum/src/ZumWebAuthn.cc` | Delete the hand-written `Attestation` scan state and `attestationVisit`. Express the validation projection over `fmt` and `authData` with reflected CBOR metadata; when `fmt` is `none`, ignore `attStmt` on input. Extract the nested COSE key through `Ztls::COSE::ES256::loadPK()`. Any typed none-attestation output must include `attStmt` as a definite empty map. |
| Zum CBOR policy | `zum/src/ZumPasskey.hh`, `zum/src/ZumPasskey.cc`, `zum/src/ZumDB.hh`, `zum/src/ZumDB.cc`, `zum/src/ZumServer.hh`, `zum/src/ZumServer.cc` | Delete `cborLimits` members, parameters, configuration, forwarding, and defaults. Enforce any application message-size policy at the owning input boundary before calling `ZfCBOR`; do not recreate codec counters under new names. |
| Zum tests | `zum/test/ZumTest.cc` | Delete codec-limit configuration tests and retain registration, malformed attestation, malformed COSE key, and input-boundary size-policy coverage where such a policy actually exists. |

Keep the reflected COSE and attestation schemas local to their owning
components unless another real consumer exists.  Do not move protocol policy
into `ZfCBOR`: Zf parses CBOR, Ztls validates COSE/ES256, and Zum validates the
WebAuthn attestation profile.  This preserves module boundaries and addresses
[Framework fit](GUIDELINES.md#framework-fit) and
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

The reflected ES256 COSE object recognizes integer labels `1` (`kty`), `3`
(`alg`), `-1` (`crv`), `-2` (`x`), and `-3` (`y`) but permits any additional
COSE labels.  Unknown keys and values are structurally validated and skipped by
`ZfCBOR`; they do not make the typed object incompatible.  Repeated recognized
labels follow the codec-wide rule that the last occurrence wins.  Ztls then
validates the final recognized values: all five are present, `kty == 2`,
`alg == -7`, `crv == 1`, both coordinates are exactly 32 bytes, and the
resulting public point is valid.  Those are COSE/ES256 application checks, not
CBOR metadata rules.  Negative tests must cover missing recognized labels,
wrong final duplicate values, coordinate lengths, invalid points, compound
unknown labels, and unknown labels before and after recognized duplicates.

The reflected Zum validation projection likewise permits unknown top-level
fields, structurally validates and skips their values, and applies last-wins
semantics to recognized `fmt` and `authData` occurrences.  Unknown extensions
do not make registration fail.  Once the final `fmt` is `none`, do not require,
type-check, or inspect `attStmt`; its emptiness carries no information needed by
this relying party, so validating it would add redundant application work.

Emission remains conformant: represent a none-attestation output, wherever one
is required, with a required zero-field reflected `attStmt` object and save it
as the definite empty map `0xa0`.  Do not allocate a map or add an empty-map
codec primitive.  Input permissiveness does not justify emitting an omitted,
non-map, indefinite, or nonempty `attStmt`.  Add an exact-output fixture for
`{"fmt": "none", "authData": bytes, "attStmt": {}}`.

Migration must be atomic in this order:

1. complete `ZfCBOR` and its tests;
2. replace the Ztls callback parser with a typed projection and update tests;
3. replace the Zum callback parser with a typed projection, delete all
   `cborLimits` plumbing, and update tests;
4. delete superseded declarations and implementation fragments from the
   retained dependent files; and
5. build and test the changed layers through `Zum` before considering the
   change complete.  Do not rebuild or retest layers downstream of `Zum` as
   part of this change.

After migration, search the entire repository rather than relying on the
compiler to find every stale path.  Review every tracked source, header, test,
example, script, and build input that can use the codec; the current inventory
must not be treated as an exclusion list.  Generated output, documentation, and
planning artifacts are not dependent source code and do not imply deleting any
file:

```sh
rg -n 'using Visit|ZfCBOR::Item|COSEContext|coseVisit|attestationVisit' \
  . --glob '*.{hh,cc,am}' --glob '!cbor.md'
rg -n 'ZfCBOR|cborLimits|es256COSEPublicKey|Ztls::es256|Ztls::ES256' . \
  --glob '*.{hh,cc,am}' --glob '!cbor.md'
rg -n 'ZfCBOR.*(Legacy|Compat|Old)|(?:Legacy|Compat|Old)ZfCBOR|CBOROld' . \
  --glob '*.{hh,cc,am}' --glob '!cbor.md'
```

The first and third commands must return nothing.  The second command must have
no `cborLimits` result; review every other result against the table above.
Permitted results must refer only to the completed codec, its direct consumers,
or their tests.  Also inspect the final diff for copied portions of the old scanner or
callbacks under new names; textual renaming does not constitute deletion.

## Delivery

### 1. Complete scanner tests first

Extend the existing `zf/test/ZfCBORTest.cc` in the style of `ZfASN1Test` and
`ZfStructTest`, using compact repository-owned fixtures selected to exercise
the implementation.  Standard RFC or public-corpus vectors may be copied or
adapted where useful, but the suite need not mirror any external corpus or
claim comprehensive standards conformance.  Prefer readable inline byte
fixtures and locally generated boundary cases over a separately versioned data
package.  Cover:

- every major type and argument-width boundary;
- `UINT64_MAX`, `INT64_MIN`, and negative values below `INT64_MIN` represented
  by their CBOR argument;
- definite/indefinite strings, arrays, and maps, including every invalid break
  and chunk case;
- nested and unknown tags;
- all simple values and reserved additional information;
- binary16/32/64 edge classes, normalization of representative NaN encodings
  to canonical output, negative zero, and RFC 8949 preferred-serialization
  examples; verify shortest-exact width at every exponent/significand boundary
  and binary64 fallback for inexact narrowing;
- opaque text bytes, truncation at every byte, and successful prefix
  consumption; and
- typed text tests verifying delegation to `ZuUTF`.

Keep COSE-key fixtures and their semantic validation tests in `ztls/test`;
keep WebAuthn attestation fixtures and policy validation tests in `zum/test`.
The Zf test only verifies that their generic CBOR constructs are supported,
without naming, constructing, or validating either protocol's messages.

Add malformed cases beside the valid construct they target, and keep any
deterministic mutation/fuzz regression seeds in the same test target unless a
real fuzzing workflow later justifies a separate corpus.  Use `ZuTestUtil`, as
required by [Tests](GUIDELINES.md#tests).

### 2. Complete scanner

Replace the current callback scanner with the bounded recursive validation and
skip engine.  Keep it allocation-free.  Update the focused test until every
wire case passes before adding metadata templates.

### 3. Add typed load/save

Add the CBOR facet, properties, `LoadContext`, handlers, type conversions, and
direct encoder.  Test:

- every supported `ZfFieldTC` scalar and vector;
- nested objects and arrays, pointers, ctor/init fields, optionals, mixed text
  and integer object IDs, unknown fields of every CBOR key type, last-wins
  duplicate keys, and the ordinary compile-time save filter;
- integer-ID boundary values, asserted invalid `IntID` spellings in a
  debug-only schema test, and exact `-42` to `"-42"` load/save correspondence;
- exact output bytes, not only round trips;
- `load()`'s `ZfASN1`-compatible sequential mutation semantics; and
- discriminator projection followed by RSA/EC-style selected schema loading.

Cover the fixed/decimal/time mappings with exact range, scale, clamp, and
precision-loss tests before claiming complete `ZfStruct` coverage.

### 4. Migrate and delete superseded code

Execute the complete dependency table and atomic migration sequence above.
Within the retained files, delete both callback parsers and all superseded
scanner and call-site code as each use is replaced by the completed `ZfCBOR`
API.  Do not delete a dependent file merely because it previously used the old
codec; delete it only if removing the superseded implementation leaves the
whole file dead, and remove the corresponding build references.  Run the
deletion searches before tests; a green focused test is insufficient while any
old code path remains.

### 5. Harden

Add a deterministic mutation test or fuzz target for scanner and typed load.
Keep the scanner and discriminator paths allocation-free by construction.
Exercise `ZtScratch` and `LoadVec` heap fallbacks for correctness and leaks.
Follow the existing `ZfASN1` structure and the applicable `GUIDELINES.md`
hot-path rules.  Performance tuning is subsequent work if production evidence
requires it and is not a completion gate for this implementation.

Configure and run the clang debug build and its Valgrind checks through the
last changed dependent layer:

```sh
make -C zf/test ZfCBORTest -j
libtool exec valgrind --error-exitcode=1 ./zf/test/ZfCBORTest
make -C ztls/test ZtlsSecTest -j
libtool exec valgrind --error-exitcode=1 ./ztls/test/ZtlsSecTest
make -C zum/test ZumTest -j
libtool exec valgrind --error-exitcode=1 ./zum/test/ZumTest
```

This implementation's requested verification scope is clang debug and Valgrind
only; GCC, ASan/LSan, MinGW, the top-level test suite, and tests downstream of
`Zum` are not part of this change gate.  No test may allocate from an
attacker-declared size before validating it against the remaining input span.

## Completion criteria

- One bounded recursive scanner handles the complete RFC 8949 wire grammar.
- The scanner allocates nothing; object and array span indexes use stack or
  builtin storage for representative workloads and their named heap fallbacks
  only when those capacities are exceeded.
- No dynamic tree, union wrapper, parser registry, or node heap exists.
- `ZfStruct` object loading uses a `ZtScratch` field-span index and direct
  conversion.
- Arrays use the compact `LoadVec` span index and reflected maps index only
  recognized field spans, without an intermediate value tree; no `AsMap`
  exists.
- Discriminated alternatives use projection, switch, and selected reparse.
- Encoding writes directly, uses no value stash, emits preferred shortest-exact
  floats, and emits reflected maps in compile-time deterministic key order.
- `COSEContext`, `coseVisit`, and `attestationVisit` are deleted, and their
  protocol checks use reflected schemas over the completed direct-span codec.
- The public `Item`/`Visit` callback contract is deleted; no replacement
  untyped traversal API is introduced.
- All `cborLimits` references are deleted; remaining `ZfCBOR` references have
  been individually audited as consumers of the completed implementation.
- No legacy/compatibility CBOR source, overload, alias, test, or build target
  remains.
- Every retained API and identifier has a demonstrated consumer and conforms to
  `GUIDELINES.md`.
- Zf, Ztls, and Zum focused tests pass under the clang debug build and
  Valgrind with no reported memory-access errors.
