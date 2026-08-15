# `ZfTOML` / `ZvTOML` implementation prescription

This document is normative for the implementation. `Must`/`do` items are
requirements, not suggestions. Where it is silent, copy the corresponding
`ZfCf`/`ZvCf` code rather than inventing another abstraction. Follow the
structure and review standard of `yaml.md`, adjusted only where TOML's data
model, document grammar, or canonical output requires a real difference.

## Objective

Add `ZfTOML` beside `ZfCf` and `ZfYAML` in `zf`, and add `ZvTOML` beside
`ZvCf` and the concurrent `ZvYAML` work in `zv`. The implementation must use
the canonical `ZfTree::AnyNode` representation and the common `ZfTreeLoad`
loader. It must retain the Cf/YAML public layering, error style, allocation
strategy, short scanner names, mmap-backed file I/O, and test organization.

Use TOML v1.1.0 as the language reference:

- <https://toml.io/en/v1.1.0>
- <https://github.com/toml-lang/toml/blob/1.1.0/toml.abnf>
- <https://github.com/toml-lang/toml-test>

Apply Postel's Law at the input boundary: accept the established v1.1 forms and
benign mainstream variants rather than enforcing a needlessly narrow version
gate, while emitting one deterministic conservative form. In particular,
accept heterogeneous arrays, multiline and trailing-comma inline tables, `\e`,
`\xHH`, and date/time values with omitted seconds. Tolerance does not extend to
ambiguous structure, duplicate definitions, type collisions, malformed quoted
content, or inputs that cannot be represented safely in `ZfTree`.

## Scope contract

Implement one complete TOML document per `scan()`/`load()` call:

- The returned root is always an `AnyNode::Object`, including for an empty or
  comment-only input. TOML has a mandatory implicit root table and has no root
  scalar or root array syntax.
- Support bare, basic-string, and literal-string key segments; dotted keys;
  ordinary table headers; inline tables; arrays; and arrays of tables,
  including nested arrays of tables and sub-tables of the most recently
  appended array element.
- Support all four string forms: basic, multiline basic, literal, and
  multiline literal. Decode escapes into owned UTF-8, implement multiline
  opening-newline trimming and basic-string line continuation, and normalize
  CRLF to LF in decoded multiline strings.
- Support decimal, hexadecimal, octal, and binary integers; decimal floats;
  `inf`, `+inf`, `-inf`, `nan`, `+nan`, and `-nan`; lowercase booleans; offset
  date-time, local date-time, local date, and local time; heterogeneous arrays;
  multiline arrays; multiline inline tables; and permitted trailing commas.
- TOML has no null value and an omitted value after `=` is an error. Otherwise
  prefer accepting an unambiguous value over rejecting it solely for a minor
  version or presentation distinction.
- Decode all keys and strings into owned node storage. Comments, quoting style,
  numeric base, numeric separators, table-header spelling, and source layout
  are presentation details and are not retained.
- Keep deterministic object-field order based on first semantic definition.
  A later legal addition to an implicitly created table appends at that point;
  it does not relocate earlier fields.
- Treat keys as exact Unicode scalar sequences without normalization. Bare and
  quoted spellings that decode to the same bytes identify the same key.
- Enforce TOML's definition rules, not merely final-tree duplicate detection:
  reject duplicate keys, scalar/table collisions, table redefinition, dotted
  key redefinition, extending a sealed inline table, converting a static array
  to an array of tables, converting a table to an array of tables or vice
  versa, appending to a statically defined array, and addressing an array of
  tables before its owning element exists.
- Fully define inline tables at their closing brace. No later dotted key,
  table header, or array-of-tables header may add to them or their descendants.
- For an array-of-tables path, resolve each array segment through its most
  recently appended object. Never interpret a dotted numeric key segment as
  an array index.
- Use `ZuUTF` for UTF-8 traversal, validation, and escaped scalar handling
  rather than adding a second Unicode validator. Silently consume a UTF-8 BOM
  when it occurs at the beginning of the document. Do not scan specifically
  for BOM elsewhere; within ordinary content it is handled by `ZuUTF` and the
  enclosing TOML production. Accept LF, CRLF, and benign bare-CR line endings,
  normalizing decoded multiline content to LF. Preserve ordinary valid
  non-ASCII bytes exactly.
- Do not copy Cf-only `${...}` expansion, `%define(...)`, arbitrary `%...`
  callbacks, or `%include(...)`. TOML comments and strings must not acquire
  application-specific expansion semantics.
- Do not copy YAML's OpenAPI/schema transformation layer. `$ref`, `allOf`,
  `oneOf`, and `anyOf` are ordinary TOML keys with no parser-level meaning;
  `ZfTOML` exposes no `resolve()` or `flatten()` API and carries no associated
  clone, merge, JSON Pointer, or schema-transform diagnostics/tests. Those
  facilities are requirements of `ZfYAML`, not of the shared tree or TOML.
- Do not retain comments or promise formatting-preserving round trips. This is
  a semantic configuration parser and deterministic emitter, not a source
  editor.
- Do not add a third-party TOML parser or a configure-time dependency.

Describe the result as a mainstream, TOML v1.1-oriented configuration parser
and canonical emitter. Unlike the deliberately bounded YAML profile, it covers
the full TOML data model, but input acceptance intentionally follows Postel's
Law rather than advertising byte-for-byte conformance policing.

## Representation and common-layer precursor

### Shared tree

`ZfTOML.hh` includes `ZfTree.hh` and re-exports the canonical names exactly as
the other format facades do:

```cpp
using ZfTree::AnyNode;
using ZfTree::Node_;
using ZfTree::Node;
using ZfTree::NodeArray;
using ZfTree::CNodeArray;
using ZfTree::newNode;
namespace ValueTC = ZfTree::ValueTC;
namespace ScalarTC = ZfTree::ScalarTC;
```

`ZfTOML::AnyNode`, `ZfYAML::AnyNode`, `ZfCf::AnyNode`, and
`ZfTree::AnyNode` must be compile-time-identical. Do not introduce a TOML DOM,
wrapper node, shared graph, parsed-number payload, or table subclass. Continue
using the common `AnyNode` typelist, mutable `parent`, one-byte `scalarType`,
unique `ZuPtr` ownership, builtin container sizing, and the common
`"ZfTree.Node"` heap.

Collection nodes retain `ScalarTC::None`. Strings use `String`, both boolean
values use `False`/`True`, and integers/floats use `Number`. Store an accepted
numeric token exactly as it appeared after delimiting it: preserve its sign,
base prefix, `_` separators, exponent case, decimal spelling, and special-float
spelling. Do not convert hexadecimal, octal, or binary integers to decimal and
do not otherwise canonicalize numeric lexemes in the tree.

Validate and classify the complete token while parsing, including the signed
64-bit integer bound, but leave target conversion to TOML-aware field loading.
This retains precise source diagnostics, avoids an unnecessary rewrite/copy,
and permits later integer/float/fixed/decimal loaders to apply their own target
range and representation rules directly to the TOML spelling.

### Date/time value

TOML date/time tokens are not strings. Add one common parsed date/time data type
in a small, separately reviewed `ZfTree`/`ZfTreeLoad` precursor:

```cpp
using DateTime = ZuDateTime;
```

Append `AnyNode::DateTime` to the common node typelist and append
`ValueTC::DateTime` to its matching type enum, preserving the existing
Array/Object/String numeric values. A TOML offset date-time, local date-time,
local date, or local time is parsed immediately into this one `ZuDateTime` node
type. Do not add four TOML-specific scalar kinds and do not retain a parallel
date/time lexeme in a `String` node.

Use zero-offset `ZuDateTime` semantics for local values. A local date uses
midnight; a local time uses 1970-01-01 as its fixed neutral date; a local
date-time uses its stated wall-clock fields; and an offset date-time applies
its offset to the represented instant. Centralize these conversions in one
TOML date/time helper so parsing, loading, and canonical emission agree.

Teach the common date/time load branch to return the stored `ZuDateTime`
directly (or `as_time()` for a `ZuTime` target), while retaining the existing
string/number paths used by Cf/JSON/YAML. Ordinary string handlers must not
admit a `DateTime` node. The precursor must include direct construction,
type-enum, load, parent/path, and existing-format regression coverage and must
not otherwise change ownership or loader behavior.

### Parser-only table state

The final tree cannot express whether an object was implicit, dotted-key
defined, header-defined, or sealed inline syntax. Keep that information only
during scanning in TOML-specific side tables keyed by stable `AnyNode *`:

- object state: root, implicit, dotted-defined, header-defined, inline-sealed,
  or array-element;
- array state: static value array or array-of-tables;
- current table and, where applicable, the current array-of-tables element.

Keep the records in a `ZtScratch`-backed array with a small workload-derived
on-stack capacity and a named `ZfTOML.TableState` heap fallback. Each record
contains its stable `AnyNode *` key and compact object/array state. Use bounded
linear lookup while the records remain within that small capacity; on crossing
the threshold, build a local `ZmLHashKV<AnyNode *, unsigned>` index using
`ZmLHashLocal<>` and a named `ZfTOML.TableStateIndex` hash ID, then use it for
all subsequent lookup. This keeps mainstream documents allocation-free for
table metadata without making large table-heavy documents quadratic or
imposing a fixed table-count limit. Do not put parser flags in `AnyNode`, encode them
into `scalarType`, or use the document's object fields as a metadata index.
Discard the scratch records and optional index after `scan()` returns.

Every insertion, replacement, or append must immediately set the child's
`parent`; the returned root must have `parent == nullptr`. Detect a duplicate
leaf by linearly scanning the destination object's field array before append,
matching the small-object strategy prescribed by `yaml.md`.

## Files and build wiring

Use the counterparts only as API/layout references. Copy thin facade and file
wrapper structure where it is genuinely format-neutral, but do not begin with a
mechanically renamed Cf/YAML parser or emitter: that obscures inherited syntax,
allocation, and ownership assumptions and was a source of corrective YAML work.
Establish TOML exact-output and grammar tests before filling implementation
bodies:

- `zf/src/ZfTOML.hh` from the thin facade in `zf/src/ZfCf.hh`, cross-checked
  against `zf/src/ZfYAML.hh` for the shared-tree/policy shape;
- `zf/src/ZfTOML.cc` using the scanner organization in `zf/src/ZfCf.cc` as a
  naming/error-flow reference, with TOML-only grammar and state;
- `zf/test/ZfTOMLTest.cc` reusing reflection/loader fixtures from
  `zf/test/ZfCfTest.cc` while replacing, rather than adapting, all syntax and
  exact-output expectations with TOML cases;
- `zv/src/ZvTOML.hh` from the non-directive portions of `zv/src/ZvCf.hh`;
- `zv/src/ZvTOML.cc` from the mmap/file-error portions of `zv/src/ZvCf.cc`;
- `zv/test/ZvTOMLTest.cc` from the non-include portions of `ZvCfTest.cc`.

Update the four module `Makefile.am` files to install/compile the new headers
and sources and build/run both new tests. Do not modify unrelated Cf/YAML users
or migrate existing configuration files in this change.

## `ZfTOML.hh`

### Facet and properties

- Declare `ZuStructFacet(TOML)`.
- Add `namespace ZfTOML { using namespace ZfJSON; }` only where needed to retain
  the established field-property and byte-codec vocabulary.
- Define `ZuFieldProp::TOML` with the complete `ZuFieldProp::JSON` property
  surface, following `ZuFieldProp::Cf`: `ID`, `BytesFmt`, `NumberFmt`,
  `TimeFmt`, `Optional`, their shorthand aliases, and their `Get*` selectors.
  Import that vocabulary rather than copying its implementations, then add the
  TOML-only properties below:

  ```cpp
  namespace ZuFieldProp::TOML {
  using namespace ZuFieldProp::JSON;
  // ScalarFmt / GetScalarFmt and ArrayFmt / GetArrayFmt follow
  }
  ```

  The facet, not distinct property template identities, provides per-format
  control: `ZuStructFacet(TOML)` selects the TOML field metadata and its property
  list independently of the JSON facet. Reusing JSON's property types therefore
  does not couple a field's TOML formatting to its JSON formatting. Follow
  `ZfCf` here; do not clone `ID`, `BytesFmt`, `NumberFmt`, `TimeFmt`, or
  `Optional` merely to give them TOML-specific types.

  This provides `ID<...>` with the reflected field ID as its default;
  `BytesFmt` with `Base64` as default plus `Base64URL`, `Base32`, `Hex`, and
  `Raw`; `NumberFmt` with `Number` as default plus `String`, both parameterized
  by the existing `ZuFmt`; `TimeFmt` with `ISO` as default plus `FIX`, `CSV`,
  and `Unix`; and `Optional`/`Opt`. Expose `GetID`, `GetIDs`, `GetBytesFmt`,
  `GetNumberFmt`, `GetTimeFmt`, and `GetOptional` through the TOML namespace so
  handlers and emitters never reach into the JSON namespace directly.

  The actual formatting controls are the existing `ZuFmt`, `ZuDateTimeFmt`,
  `ZtBytesFmt`, and related framework formatters selected by those properties;
  do not introduce TOML-specific number, date/time, or byte payload formatters.
  Add the TOML-specific output-representation `ScalarFmt` and structural
  `ArrayFmt` properties described below. Formatting and structural-layout
  properties control output only; they must not restrict which valid TOML
  lexical or structural representation is accepted on input. IDs,
  optional/required status, ranges, target types, and byte-decoding properties
  retain their schema/conversion meaning. Match and load through the TOML facet
  rather than another facet.
- Declare `ZfTOML_Fmt(...)` and `ZfTOML_StringFmt(...)` ADL customization
  points, with the `AsObject`, `AsArray`, `AsString`, and default-selection
  surface matching `ZfCf`. These choose structural/string handlers; they are
  not a second scalar-formatting control system.

### Scalar output property

TOML has four string syntaxes but no YAML-style folded block scalar. Add an
output-only `ScalarFmt` property following the same tagged-property pattern:

```cpp
namespace ZfTOML {
  enum {
    NativeScalar,
    BasicScalar,
    LiteralScalar,
    MultilineBasicScalar,
    MultilineLiteralScalar
  };
}

namespace ZuFieldProp::TOML {

template <uint8_t I> struct ScalarFmt { };

using Native = ScalarFmt<ZfTOML::NativeScalar>;
using Basic = ScalarFmt<ZfTOML::BasicScalar>;
using Literal = ScalarFmt<ZfTOML::LiteralScalar>;
using MultilineBasic = ScalarFmt<ZfTOML::MultilineBasicScalar>;
using MultilineLiteral = ScalarFmt<ZfTOML::MultilineLiteralScalar>;

template <typename Props, bool = HasValue<Props, ScalarFmt>{}>
struct GetScalarFmt_ {
  using T = ZuConstant<uint8_t, ZfTOML::NativeScalar>;
};
template <typename Props>
struct GetScalarFmt_<Props, true> {
  using T = GetValue<Props, ScalarFmt>;
};
template <typename Props>
using GetScalarFmt = typename GetScalarFmt_<Props>::T;

} // ZuFieldProp::TOML
```

`Native` is the default: numbers, booleans, and date/time values use their
native selected TOML representation, while a string representation uses a
single-line basic string. `Basic` emits `"..."` with TOML escapes. `Literal`
emits `'...'` without escape processing. `MultilineBasic` emits `"""..."""`,
preserves actual value newlines, and escapes only where TOML requires it.
`MultilineLiteral` emits `'''...'''` without escape processing and preserves
actual value newlines.

The four non-`Native` styles are valid only when the field's selected output
representation is a string, including strings, encoded bytes, string-formatted
UDTs, and values explicitly configured for string rather than native numeric or
date/time output. Reject a non-`Native` style on a native number, boolean, or
date/time representation at compile time. For vector fields, the resolved
element properties select the scalar style for each scalar element.

This is deliberately narrower than YAML `ScalarFmt`: quoting a native TOML
number, boolean, or date/time changes the document's semantic type to string.
The existing number/time property must first select string output when that
conversion is intended. Keep this distinction explicit in traits and tests
rather than copying YAML scalar-style behavior into TOML.

Explicit styles are strict rather than hints. `Literal` fails saving a value
containing an apostrophe, newline, or forbidden control character.
`MultilineLiteral` fails on a run of three or more apostrophes or a forbidden
control character. Handle the trimmed opening newline of both multiline forms
so parse-back preserves a value that begins with a newline. `Basic` and
`MultilineBasic` must escape delimiter runs, backslashes, and controls without
changing the value. Report an unrepresentable requested style through a focused
`ZfTOMLError` save diagnostic; do not silently fall back to another style.
When a preformatted lexical scalar must be restyled, use stack-first
`ZtScratch` storage with a named `ZfTOML.ScalarBuf` heap fallback only on that
explicit path. Native numbers/booleans/date-time and ordinary string/basic
output remain direct-to-destination.

Do not add `BlockFolded`. A line-ending backslash inside a multiline basic
string removes source newlines and surrounding whitespace; it is an escape and
layout mechanism, not another TOML scalar representation. `MultilineBasic`
does not automatically fold, wrap, or reflow text.

### Array output property

Follow the `ZfURI` field-property pattern: define a tagged property, concise
aliases, a detector-based selector, and a default in `ZuFieldProp::TOML`:

```cpp
namespace ZfTOML { enum { InlineArray, TableArray }; }

namespace ZuFieldProp::TOML {

template <uint8_t I> struct ArrayFmt { };

using Inline = ArrayFmt<ZfTOML::InlineArray>;
using Tables = ArrayFmt<ZfTOML::TableArray>;

template <typename Props, bool = HasValue<Props, ArrayFmt>{}>
struct GetArrayFmt_ {
  using T = ZuConstant<uint8_t, ZfTOML::InlineArray>;
};
template <typename Props>
struct GetArrayFmt_<Props, true> {
  using T = GetValue<Props, ArrayFmt>;
};
template <typename Props>
using GetArrayFmt = typename GetArrayFmt_<Props>::T;

} // ZuFieldProp::TOML
```

`Inline` emits the field as an ordinary TOML array value and is the default.
`Tables` emits the field using TOML `[[array.of.tables]]` headers. `ArrayFmt`
controls output only; both source spellings parse to the same `AnyNode::Array`
and load identically.

`Tables` is valid only on a vector field whose element is an `AsObject` UDT.
Reject its use on primitive arrays, bytes, string-formatted UDTs, or a type
without reflected fields at compile time. Apply the property to the outermost
field vector only; nested vector fields carry and resolve their own properties.
For a UDT field formatted through `ZfTOML::AsArray<ZfFieldTC::UDT>`, inspect
the resolved array handler and element handler when validating `Tables`; the
enclosing `ZfStruct` field itself remains `ZfFieldTC::UDT`.

### Scan API, limits, and errors

Use this public limit type:

```cpp
struct Limits {
  enum {
    DepthMax = 128,
    NodeMax = 1<<18
  };

  unsigned depth = DepthMax;
  unsigned nodes = NodeMax;
};
```

A zero value disallows the corresponding nested operation/allocation; it does
not disable the limit. Route every parser node allocation through one private
`node<Data>(parent, ...)` helper that checks and increments `nodes` before
calling `newNode<Data>`. Count the root and every implicit table. Track nested
arrays and inline tables with `depth`; table headers and dotted key paths do not
consume C++ call stack and should be walked iteratively.

Keep maintenance comments beside `DepthMax` and `NodeMax` stating the
stack/resource bound each default protects and the workload/fixture evidence
used to choose it; they are tunable API defaults, not grammar limits.

Keep the public form parallel to YAML:

```cpp
Scan(ZuCSpan, Limits = {});
ZuTuple<int, ZuPtr<const AnyNode>> Scan::scan();
ZuTuple<int, ZuPtr<const AnyNode>> scan(ZuCSpan, Limits = {});
```

The const result follows `ZfCf`. Mainstream TOML use has no standardized
`$ref`/`anyOf`-style semantic expansion or other post-parse transform comparable
to the YAML/OpenAPI path, so exposing mutation is unnecessary. `Scan`
internally builds mutable nodes and publishes the completed immutable root.
Return the full input byte length on success.

`SyntaxError` retains offset, line, column, offending byte, and first-failure
state, and adds a one-byte code using at least:

```cpp
namespace SyntaxErrorCode {
  enum {
    Syntax, UTF8, Unicode, Control, Newline, Escape, Key, Value,
    Integer, IntegerRange, Float, DateTime, DuplicateKey,
    Redefine, TableConflict, InlineSealed, ArrayConflict,
    DepthMax, NodeMax, TrailingInput
  };
}
```

`fail()` records only the first code and location. `badSyntax()` includes a
stable short reason for non-generic codes while retaining the Cf/YAML message
shape and optional file path. Clone the conversion diagnostics from
`ZfCfError` as `ZfTOMLError`, add a `badScalarFmt` save diagnostic carrying the
field path and requested style, and define `ZfTOML_EXCEPT`.

### `TOMLPolicy` and loading

Define `TOMLPolicy` parallel to `CfPolicy`/`YAMLPolicy`:

- retain `ZfTreeLoad::ScalarMask` as the existing typed `ZtFlags`; pass
  `ScalarMask::T` through policy admission and do not regress it to raw integer
  masks or duplicate bit operations in TOML;
- provide TOML `GetIDs`, `GetBytesFmt`, `GetNumberFmt`, and `GetTimeFmt`;
- have those selectors resolve to the existing framework formatting types and
  properties; do not create TOML-only formatter types;
- select TOML nested handlers;
- admit string, number, and boolean scalar metadata according to the target
  `ZfTreeLoad::ScalarMask`, and admit `AnyNode::DateTime` only to the common
  date/time target branch;
- provide TOML-aware integer and floating conversion hooks that consume the
  preserved full lexeme, including signs, `_`, `0x`, `0o`, `0b`, exponents,
  `inf`, and `nan` as applicable to the requested target, with lexical meaning
  determined by TOML rather than by the field's output format;
- return booleans directly from `False`/`True`;
- construct all required/type/value/boolean/range/enum exceptions through
  `ZfTOMLError` only.

Add only the minimal numeric-conversion policy seam needed by `ZfTreeLoad` so
its existing integer/float/fixed/decimal branches can delegate lexical scanning
to `TOMLPolicy` without copying those branches into `ZfTOML`. Cf and YAML
policies retain their existing scanner behavior. The TOML hooks must report
full-span failure and target range errors through the existing policy error
functions.

Align the conversion branches, errors, ranges, and reflected target handling
with Cf, but not Cf's field-directed source spelling. TOML input determines its
own lexical value: an unprefixed integer is decimal, while `0x`, `0o`, and `0b`
select hexadecimal, octal, and binary. `ZuFieldProp::Hex`, `ZuFmt`, and the
other formatting properties affect subsequent TOML output and never reinterpret
or reject an otherwise valid input spelling. Reuse the existing `Zu_nscan`
machinery for decimal/hexadecimal and extend or compose it narrowly for TOML
octal, binary, and `_` separators rather than converting the stored token to
decimal.

All integer paths must be sentinel-aware and overflow-safe. Check the parsed
magnitude against the destination box's `Cmp::minimum()`/`Cmp::maximum()` and
the parser's signed-64-bit bounds, handling the negative minimum as a magnitude
without first negating it in the signed type. Consume the full lexeme before
success. Do not depend on wraparound, a decimal normalization buffer, or a
second numeric parse.

Object and array handlers derive publicly from
`ZfTreeLoad::Object<TOMLPolicy, ...>` and
`ZfTreeLoad::Array<TOMLPolicy, ...>`. The string handler derives from
`ZfTreeLoad::String<TOMLPolicy, ...>`. `loadValue` is a one-line forwarding
template. Do not copy field matching, required checks, vector loading,
conversion branches, update logic, or lazy vector machinery out of
`ZfTreeLoad`.

TOML syntax validation must not weaken target-field validation. Parsed integer
and float nodes both have `Number` kind, but the preserved full lexeme must
still cause an integer target to reject fractional/exponent/special values.
Parsed `DateTime` nodes are admitted only to the relevant time/date conversion
path; quoted strings remain strings even if their contents look like a date.

### Canonical save

JSON and Cf collection syntax are not TOML. Do not reuse either collection or
key emitter. Implement a TOML-specific deterministic emitter while retaining
the Cf facade names `save`, `saveUpd`, and `saveDel`.

Array layout is explicitly field-directed rather than selected globally or by
a size heuristic:

- require the public document root to use `AsObject`; a root scalar or array is
  not representable as a TOML document and must fail at compile time where the
  handler selection makes that possible, otherwise with `badType`;
- emit each selected assignment-form root field using the TOML key utility
  below, then ` = `, a value, and LF, in reflected field order; defer
  section/table-array fields to the header pass;
- emit nested UDT/object values as inline tables when their reflected subtree
  contains no `ZuFieldProp::TOML::Tables` field requiring table context;
- for a vector field whose `GetArrayFmt<Props>` is `InlineArray`, emit an
  ordinary array value; UDT elements are inline tables;
- for a vector-of-object field whose `GetArrayFmt<Props>` is `TableArray`, emit
  one `[[path.to.field]]` header per element, followed by that element's fields;
  recursively emit nested `TableArray` fields beneath the current element, as
  in `[[hooks.PreToolUse.hooks]]`;
- emit every selected string representation through `GetScalarFmt<Props>`;
  `Native` uses an escaped basic string, while the four explicit styles use
  their prescribed syntax and enforce their representability rules;
- emit booleans as lowercase `true`/`false`; emit finite numbers through their
  selected `ZuFmt` control, respecting the existing number-versus-string
  property; when an integer format selects a non-decimal base, include the TOML
  literal prefix needed to make that base unambiguous; emit unquoted special
  floats as lowercase `nan`, `inf`, or `-inf` when numeric form is selected;
- emit date/time fields through the selected existing `ZuDateTimeFmt` control
  and field property: a TOML-compatible ISO result may be an unquoted TOML
  date/time token, Unix form is numeric, and other textual forms are TOML basic
  strings styled through `GetScalarFmt<Props>`. Do not add TOML-specific
  date/time payload-formatting knobs;
- emit commas between array/inline-table members without a trailing comma and
  keep each non-multiline assignment-form value on one logical line;
  `MultilineBasic` and `MultilineLiteral` values may span lines, and table-array
  fields use their required header/body line structure;
- omit optional/unselected fields through the existing filters. Never emit a
  synthetic `null`.

Emit conventional TOML keys rather than quoting every field ID. A non-empty ID
containing only `A-Z`, `a-z`, `0-9`, `_`, and `-` is emitted bare; this includes
all-digit keys, which TOML still interprets as strings. Emit every other ID as
an escaped basic quoted key. Apply the same utility independently to every
dotted path/header segment so a dot or whitespace inside one ID cannot change
the tree shape. Do not reuse Cf bare-token rules or JSON key emission. The
parser must accept every legal source spelling regardless of the canonical
output choice.

Add a compile-time recursive trait over reflected fields that determines
whether a UDT contains a `TableArray` field requiring table context. A nested
object with such a descendant must be emitted as a `[path.to.object]` section,
not as a sealed inline table. An `InlineArray` of UDT elements whose element
type requires table context is ill-formed and must fail at compile time; do not
silently ignore a descendant's `ArrayFmt`.

The emitter therefore performs ordered reflection passes per table context:
first emit scalar, ordinary-array, and safe inline-object assignments; then
emit child table sections and `TableArray` fields in reflected field order.
Carry a scratch path of decoded field IDs, emit each segment with the shared
bare-or-basic TOML key utility, and recurse into each table-array element before
advancing to the next.
This schedules valid TOML without building an intermediate tree. Use
`ZtScratch`/`ZtString` with a named TOML heap fallback for paths; do not use a
fixed-size path buffer.

Keep scalar and inline-array emission streaming directly to `S`. Factor TOML
string escaping, strict literal validation, multiline delimiter handling, and
scalar emission once so normal fields, arrays, inline tables, table-array
bodies, and `ZfTOML_StringFmt` cannot diverge. `ScalarFmt` selects only the TOML
string representation; all payload formatting remains with the existing Z
formatter ecosystem. Reuse only public scalar-formatting entry points; do not
call a JSON/Cf internal value or collection emitter.

## `ZfTOML.cc` parser

### Architecture

Parse in one pass directly from `ZuCSpan`. Do not build a token vector, generic
lexer object hierarchy, or intermediate AST. Use short, format-local helpers:

- `ws(span)` skips horizontal space only;
- `trivia(span)` consumes blank/comment-only lines while advancing position;
- `eol(span)` validates a line ending/comment boundary and returns its extent;
- `eok(span, out)` scans one bare/basic/literal key segment;
- `eokp(span, parts)` scans a dotted key path into temporary owned/borrowed
  segments and rejects empty segments or missing dots;
- `eos(span, out, style)` decodes one of the four string forms;
- `bov(span)` classifies the next value without allocation;
- `eov(span, parent, depth)` parses exactly one scalar, array, or inline table;
- `eov_Array(...)` parses a bracketed value array;
- `eov_Object(...)` parses an inline table;
- `eon(span, out, scalarType)` validates and classifies a numeric token while
  preserving its exact spelling;
- `eot(span, out)` recognizes a TOML date/time token and parses it into a
  `ZuDateTime`;
- `put(table, path, value, definitionKind)` walks/creates dotted tables and
  performs all collision and sealing checks;
- `header(span)` selects/creates an ordinary table or appends/selects an array
  of tables.

Keep monotonically updated offset/line state and the `{int, value}`
negative-return convention from Cf/YAML. Helpers record the first failure and
callers propagate `-1` without replacing it. Use file-local integer enums and
`switch` for value style, string style, numeric kind, header kind, and table
state.

Temporary dotted-key segments use `ZtScratch`/`ZtArray` with a named
`ZfTOML.KeyPath` heap fallback. Exact-copy unescaped key/string spans directly
into final owned node strings when possible. Do not use STL containers,
regular expressions, streams, locale-dependent classification, or fixed-size
path arrays.

### Keys, definitions, and tables

- Bare keys contain only `A-Z`, `a-z`, `0-9`, `_`, and `-`, and may be all
  digits. Quoted key segments use the single-line basic/literal string rules;
  empty quoted keys are valid.
- Ignore spaces/tabs around `=` and dots as allowed by the grammar. A key/value
  pair begins and ends on one physical line except that its value may itself be
  a multiline string, array, or multiline inline table.
- Dotted keys are relative to the current table. Headers are absolute from the
  root, with array-of-tables segments selecting their latest element.
- Create missing super-tables as `implicit`. Upgrade a legal implicit table to
  `header-defined` when its header later appears. Do not upgrade a table that a
  dotted key already fully defined or one that is sealed inline.
- A dotted key defines every table segment it explicitly traverses under its
  current definition scope. Track enough state to enforce the v1.1 examples
  concerning later headers and additions; final shape alone is insufficient.
- `[[path]]` creates an array on first use and appends a new object on every
  later use. Sub-table/header traversal through that array selects only its
  latest element. Reject an absent required owning element and every static
  array/table collision.
- At `}` mark the complete inline object and all tables defined within that
  inline expression sealed against later external extension.
- Reject duplicate decoded keys before moving the value into its destination.
  Failure must not leave a partially appended field or implicit table chain.
  Preflight the complete destination path and node budget, then commit.

### Strings and Unicode

- Basic strings implement `\b`, `\t`, `\n`, `\f`, `\r`, `\e`, `\"`, `\\`,
  `\xHH`, `\uHHHH`, and `\UHHHHHHHH`. Use `ZuUTF` to decode and validate
  escaped scalar values; reject malformed digit counts and escapes that
  `ZuUTF` cannot represent.
- Literal strings perform no escape processing. Single-line literal strings
  cannot contain `'` or a newline.
- Multiline strings trim exactly one newline immediately after the opening
  delimiter, allow the delimiter-specific one/two quote edge cases, and reject
  illegal delimiter runs.
- In multiline basic strings, a line-ending backslash removes itself plus all
  following whitespace/newlines through the next non-whitespace character or
  closing delimiter. No analogous folding occurs in literal strings.
- Normalize CRLF and bare CR to LF in decoded multiline values.
- At byte zero only, recognize and consume the UTF-8 BOM before parsing any
  trivia. After that, traverse and decode with `ZuUTF`; do not add a separate
  whole-document validation pass or an interior-BOM search. Escaped code points
  are emitted as UTF-8 into the final owned string.

### Numbers and date/time

- Implement integer/float recognition without regex or locale-dependent C
  conversion. Accept the v1.1 spellings and benign unambiguous presentation
  variants under Postel's Law, and preserve the complete token for TOML-aware
  field conversion. Do not reject merely because a form was added after TOML
  v1.0.
- Support the full signed 64-bit integer range and reject overflow as
  `IntegerRange` during scanning. Base-prefixed integers are non-negative.
- Recognize ordinary floats, signed zero, infinities, and NaN without replacing
  the preserved token with a binary64 value. Target loading performs the
  requested binary/fixed/decimal conversion and range checks. Reject malformed
  input, but do not add stricter conformance checks with no representation
  benefit.
- Recognize offset date-time, local date-time, local date, and local time,
  including `T`/`t`/space, `Z`/`z`, fractional seconds, and omitted seconds.
  Delegate calendar/scalar parsing to the existing `ZuDateTime`/`ZuUTF`
  machinery wherever possible instead of duplicating stricter validators.
- Construct one `AnyNode::DateTime` for every accepted date/time form. Use
  1970-01-01 as the neutral date for a local-time-only value, midnight for a
  local-date-only value, and zero offset for local values. Discard the original
  TOML date/time subtype after construction; all four forms reflect and load as
  `ZuDateTime`.

### Arrays and inline tables

- Arrays admit all TOML value types and may mix them under v1.1. They may span
  lines and contain comments/trivia around values and commas. Permit one
  trailing comma and reject omitted elements.
- Inline tables use comma-separated key/value pairs and the same dotted-key
  insertion rules within their own scope. Under v1.1 they may span lines,
  contain comments where ordinary trivia is allowed, and have one trailing
  comma. Reject an omitted field/value.
- Both constructs use the shared `eov()` dispatcher, enforce `DepthMax`, and
  return fully owned children with repaired parent pointers.

### Document scan

`Scan::scan()` executes one fixed sequence: reject a null input span; consume
one UTF-8 BOM if present at byte zero; create the root object; consume
blank/comment lines; repeatedly parse exactly one key/value line, ordinary
table header, or array-of-tables header; consume trailing trivia; require EOF;
and return `{input.length(), root}`. There are no document start/end markers
and no second-document concept in TOML. A stray non-trivia byte after a
complete construct is `TrailingInput` or the more specific syntax code at the
earliest stable byte.

## `ZvTOML.hh` and `ZvTOML.cc`

Mirror the non-directive YAML/Cf file layer:

```cpp
ZvExtern ZuTuple<int, ZuPtr<const AnyNode>> load(
  const Zi::Path &, ZfTOML::Limits = {});
```

- Define `ZvTOMLError`, `ZvTOML_EXCEPT`, and file open/stat/mmap/read/write
  diagnostics using the existing style.
- Retain `enum { MaxFileSize = 1<<20 };` and reject
  `length >= MaxFileSize`, matching `ZvCf`/the YAML plan.
- Keep `ZiStat`, read-only `ZiMMapFile`, stable backing for empty spans,
  canonical-path diagnostics, and the guarantee that the returned tree owns
  every key/scalar after the mapping is destroyed.
- Do not copy `ZvCf::Reader`, include paths, defines, expansion callbacks, or
  include-chain diagnostics. Keep one file-local `Mapped` and `map(path)`.
- Catch a parser exception only to rethrow the first syntax error with the
  canonical file path, line, column, offset, byte, and reason code.
- Keep `save`, `saveUpd`, and `saveDel` as `ZiFileTxStream` wrappers over the
  TOML emitter with matching open/flush/write handling.
- Use `.toml` in tests; the loader itself does not enforce an extension.

## Tests

### `ZfTOMLTest`

Adapt the reflection and ownership fixtures from `ZfCfTest`, retain the shared
loader integration matrix, and replace Cf grammar cases with TOML cases. Do
not duplicate format-neutral node/layout tests from `ZfTreeTest`. Cover at
least:

1. Owned source lifetime, immutable input, root-object behavior, parent
   pointers, repeated handler loads, blank/comment-only input, LF/CRLF/bare-CR,
   byte-zero BOM consumption, and `ZuUTF` handling of malformed UTF-8.
2. Bare/basic/literal keys, empty quoted keys, dotted keys with whitespace,
   Unicode keys, numeric-looking keys, and equivalence/duplication across key
   spellings. Pin canonical bare output for every safe-key character class and
   basic quoted output for dots, whitespace, controls, and Unicode; verify each
   dotted header path quotes only the segments that require it.
3. All four string forms; every escape including v1.1 `\e`/`\x`; BMP and
   non-BMP Unicode; multiline opening trim, line continuation, quote-run edge
   cases, newline normalization, and representative malformed escapes/scalars.
4. Decimal/base integers, signs, separators, boundaries and overflow; ordinary
   floats, exponents, signed zero, separators, `inf`/`nan`, invalid lexical
   forms, exact preservation of accepted lexemes, and their correct loading into
   reflected integer/float/fixed/decimal fields where semantically supported.
   Verify unprefixed decimal and each explicit `0x`/`0o`/`0b` base independently
   of every field-selected output format, including `ZuFieldProp::Hex`. Cover
   the signed 64-bit minimum without signed negation, maximum, one-step
   underflow/overflow, target boxed sentinels, and full-span rejection.
5. All four date/time source forms, offsets, leap days, fractional precision,
   `T`/`t`/space and `Z`/`z`, v1.1 omitted seconds, invalid calendar/clock
   values, construction of the common `AnyNode::DateTime`, the 1970-01-01 and
   midnight defaults, quoted lookalikes remaining strings, and reflection of
   every accepted form to `ZuDateTime`.
6. Empty/nested/multiline/heterogeneous arrays, arrays containing inline
   tables, comments around separators, trailing commas, and rejected omitted
   elements or delimiters.
7. Empty/nested/multiline inline tables, dotted keys inside inline tables,
   comments, trailing commas, and sealing against every later extension form.
8. Root and nested table headers, omitted super-tables, later legal definition
   of implicit super-tables, out-of-order tables, empty tables, and exact
   object order/parent paths.
9. Arrays of tables, repeated appends, nested arrays of tables, ordinary
   sub-tables of the latest element, ordering constraints, and every collision
   with static arrays/tables/scalars.
10. A table-driven matrix for duplicate keys, redefinitions, dotted-key
    conflicts, table/array transitions, inline sealing, and failures that occur
    only because source definition state differs despite an otherwise possible
    final tree shape. Pin reason, byte offset, line, and column.
11. The complete `ZfCfTest` field conversion matrix adapted to TOML:
    required/default/update, ranges, booleans, integers and number formats,
    enums/flags, float/fixed/decimal, date/time, byte codecs/raw bytes, vectors,
    nested UDTs, and string-formatted UDTs. Verify wrong TOML scalar kinds fail
    before conversion. For each presentation/layout property, verify that it
    changes saved output but does not gate any TOML-valid input representation
    compatible with the reflected target type. Compile-time test every imported
    JSON-surface selector through `ZuFieldProp::TOML`, including its default and
    override; verify `ID` controls both input field matching and emitted keys,
    and verify every `BytesFmt` codec on load and save. Add one dual-facet UDT
    whose JSON and TOML facets use different IDs and number/scalar formatting;
    assert the two `Field::Props` lists and exact outputs remain independent
    despite sharing the JSON property type vocabulary. Compile-time assert the
    TOML policy's scalar-admission signature uses `ScalarMask::T`.
12. `save`, `saveUpd`, and `saveDel` canonical syntax, escaping, special
    floats, date/time, nested inline tables/arrays, parse-back semantic round
    trips, and an assertion that no JSON object syntax or synthetic null is
    emitted at document root. Pin conventional bare-key TOML output for an
    ordinary reflected UDT so neither the JSON nor Cf collection emitter can
    accidentally become the default.
13. Compile-time `GetArrayFmt` default/override selection; ordinary primitive
    and UDT arrays using `Inline`; root and nested vector-of-object fields using
    `Tables`; nested `[[parent.children]]` paths; multiple elements and sibling
    table arrays; filtered `saveUpd`/`saveDel`; and parse-back equality between
    inline and table-array output. Load both valid input representations into
    the same field regardless of its selected `ArrayFmt`. Expose/test the
    relevant detector traits so invalid `Tables` element types and invalid
    `Inline` UDT element types are compile-time false before the emitter's
    diagnostic `static_assert`.
14. An exhaustive table-driven `ScalarFmt` matrix. Include every enum value and
    shorthand—`Native`, `Basic`, `Literal`, `MultilineBasic`, and
    `MultilineLiteral`—and compile-time verify `GetScalarFmt` default/override
    selection. For every applicable style, pin exact delimiters/escapes and
    parse-back equality for a root-table assignment, nested table field, inline
    table field, scalar array element, and array-of-tables body; exercise
    `save`, `saveUpd`, and `saveDel` filtering. Cross each style with ordinary
    strings, encoded bytes, string-formatted UDTs, and explicitly
    string-formatted numeric/date values where applicable. Cover empty text,
    apostrophes, single/double/triple quote runs, backslashes, tabs, forbidden
    controls, Unicode, and leading/interior/trailing newlines, including the
    multiline opening-newline rule. Assert the focused failure for every value
    unrepresentable by strict `Literal` or `MultilineLiteral`, and compile-time
    rejection of every explicit string style on native number, boolean, and
    date/time output. Independently load all four valid TOML input string
    syntaxes into each compatible fixture regardless of its selected output
    style. No `ScalarFmt` enum value may be added without extending this matrix.
15. Small custom `Limits` values proving exact node/depth boundary behavior and
    atomic failure without partially linked children. Exercise table-state
    lookup both below and above the stack-to-hash promotion threshold.

Use table-driven `ZuTestRepeat` cases and examples from the released v1.1 spec.
Vendor a curated, repository-owned subset of `toml-test` fixtures alongside
focused unit cases. Tests must not depend on network access, Go, or an external
checkout; do not vendor or run the complete upstream corpus in routine CI.

### `ZvTOMLTest`

Adapt the non-include portions of `ZvCfTest`:

- mmap-backed load and lifetime after unmapping;
- empty, missing, malformed, oversized, unreadable, and read-only files;
- canonical file path plus reason/line/column/offset in diagnostics;
- nested tables and arrays-of-tables loaded into reflected fixtures;
- file `save`/`saveUpd`/`saveDel` followed by reload and semantic comparison;
- open/write failures for invalid destinations.

## Mandatory implementation audit

Do not treat passing functional tests as completion. Before final verification,
audit every applicable red and amber flag in `GUIDELINES.md` and record either
the repair or why the flag is inapplicable. At minimum, inspect the following
areas that required post-completion YAML correction:

- Language/style: no anonymous namespaces, `enum class`/typed enums, concepts,
  non-specific `[&]`/`[=]` captures, STL containers/algorithms, C++ wrappers
  where the C header is equivalent, unnecessary casts, or forwarding wrappers
  where a `using` declaration suffices. Keep `.cc` utilities file-scope
  `static`, concise, and use integer enums plus `switch` for discrete state.
- Reuse/structure: keep `ZfTreeLoad` format-neutral and policy-driven; do not
  copy its field matching, collection/vector loading, scalar conversion, or
  diagnostics. Factor repeated UTF-8, key quoting, scalar encoding, numeric
  scanning, and parent-link work once without introducing weak abstractions.
  Delete dead compatibility paths rather than retaining Cf/YAML history.
- Allocation/data movement: enumerate every allocation in scan, load, emit,
  mmap, and error paths. Give every dynamic Z container/string a named,
  tunable heap ID; use `ZtScratch` for temporary hot-path storage with heap
  fallback; write directly into final uninitialized storage; and remove hidden
  string/byte copies, temporary contiguous conversions, and initialization
  immediately overwritten. The default native/basic scalar and ordinary-key
  emitter paths stream directly to the destination without a scalar staging
  buffer.
- Capacity/layout: replace unexplained capacities with prominently named,
  workload-derived constants and maintenance comments. The table-state scratch
  promotion threshold must be measured/justified and must not impose a hard
  document limit. Inspect and record `sizeof` plus member layout for `Scan`,
  syntax/error state, table-state records, hash values, and every recursive or
  iterative frame; reorder members and factor oversized frames before approval.
- Algorithms/control flow: cache invariant lengths/counts outside loop
  conditions, bound any intentional linear scan to the documented small-object
  path, promote table-state lookup before it becomes quadratic, avoid redundant
  parent walks and second parses, flatten nested error control flow, and scope
  container iterators to the traversal that owns them.
- Ownership/lifetime: use the one canonical uniquely owned `AnyNode`, set
  `parent` immediately on every link/move, leave no partially linked tree on
  failure, and prove source/mmap independence. Parser-only state and scratch
  indices are destroyed deterministically at return and never escape through a
  node or callback.
- Format separation: compare exact ordinary output against TOML examples, not
  merely parse-back equality. Confirm that neither Cf nor JSON collection/key
  emission is reachable by default, that shared JSON property types remain
  facet-isolated, and that every Cf/YAML divergence is explicitly TOML-driven.
- Header/build hygiene: follow the required header skeleton and direct include
  ordering, keep names within the repository limit, preserve hard-tab layout,
  and verify no TOML dependency leaks into lower common headers beyond the
  separately reviewed generic date/time and numeric-policy seams.

## Implementation sequence

1. Freeze the Postel-style TOML v1.1-oriented input profile in focused tests,
   including trailing commas, omitted date/time seconds, BOM handling, and
   other deliberately accepted benign variants.
2. Land the minimal `AnyNode::DateTime`/common-loader precursor with
   `ZfTreeTest`, `ZfCfTest`, and `ZfYAMLTest` green. Do not combine unrelated
   tree or loader refactoring with it.
3. Add makefile wiring and compiling facade skeletons: facet/properties,
   explicit tree re-exports, errors, limits, `TOMLPolicy`, thin
   `ZfTreeLoad`-derived handlers, and file APIs.
4. Implement single-line key/value parsing, the four scalar families, root
   insertion, comments/newlines, and precise diagnostics. This gives early
   end-to-end handler/load coverage.
5. Add arrays and inline tables using the common `eov()` dispatcher and depth/
   node budgets.
6. Add dotted keys and ordinary tables with parser-only definition metadata,
   collision preflight, and atomic commit.
7. Add arrays of tables and latest-element traversal, then complete the full
   redefinition/sealing/conflict matrix.
8. Add multiline strings, UTF-8/control validation, line continuation, and
   every v1.1 lexical edge case.
9. Implement the field-directed streaming TOML emitter: strict `ScalarFmt`
   encoding, `ArrayFmt` selection, the recursive table-context trait, ordered
   assignment/header passes, nested table-array paths, and reflected
   save/load/update/delete round trips.
10. Implement the `ZvTOML` mmap and file-save wrappers and file diagnostics.
11. Perform the mandatory `GUIDELINES.md` red/amber audit, including allocation
    enumeration and recorded state/frame layouts, and repair every finding.
12. Run the focused tests and curated repository-owned `toml-test` subset,
    then audit every divergence from Cf/YAML as TOML-driven.

Do not refactor `ZfTree`, `ZfTreeLoad`, `ZfCf`, or the concurrent YAML parser
while introducing TOML except for the isolated date/time value precursor. A
new common-layer defect discovered during implementation should be fixed in a
separate small change with direct common-layer and existing-format coverage.

## Verification and acceptance

Build lower layers before their tests:

```sh
make -C zf/src -j8
make -C zf/test -j8
./zf/test/ZfTreeTest
./zf/test/ZfCfTest
./zf/test/ZfYAMLTest
./zf/test/ZfTOMLTest
make -C zv/src -j8
make -C zv/test -j8
./zv/test/ZvCfTest
./zv/test/ZvYAMLTest
./zv/test/ZvTOMLTest
make -j8
make test
```

Run the matrix in warning-clean GCC and Clang configurations. For ASAN/LSAN or
any other build-type change, reconfigure through `z.config`, then run a
top-level `make clean` and `make -j8` before executing tests so stale objects or
mixed libraries cannot mask defects. Run focused ASAN/LSAN tests and use
`libtool exec valgrind --leak-check=full` for the source-tree TOML binaries; do
not execute `.libs` binaries directly. Record environmental tool failures
separately from code failures. A failure or hang in an unrelated repository
test does not erase focused evidence, but must be isolated and reported rather
than silently treating `make test` as successful.

Before completion:

- gcc and clang builds have no warnings;
- there remains exactly one node representation and one field-load algorithm;
- `ZfTreeLoad::ScalarMask` remains a typed `ZtFlags` interface in Cf, YAML, and
  TOML policy calls;
- all four format aliases of `AnyNode` are compile-time-identical;
- TOML files contain no copied `ZfTreeLoad` object/array/vector/conversion
  implementation and common headers gain no TOML parser/emitter dependency;
- existing Cf and YAML behavior and tests remain green;
- every released v1.1 data/grammar category and deliberately accepted benign
  variant is represented by focused tests; invalid tests concentrate on
  ambiguity, malformed values, collisions, and unsafe/unrepresentable input
  rather than conformance trivia;
- table/dotted-key/inline/AoT state conflicts fail deterministically at the
  first stable byte without a partially mutated returned tree;
- all keys/scalars survive destruction of the source span/mmap and every parent
  pointer/path remains correct;
- canonical output reparses to the same reflected values and is accepted by at
  least one independent v1.1 implementation through the interoperability
  harness;
- canonical ordinary keys use conventional bare TOML spelling when legal and
  quote unsafe path segments without changing their decoded IDs or hierarchy;
- `ZuFieldProp::TOML` exposes the complete JSON-compatible `ID`, bytes, number,
  time, and optional property/selector surface without duplicating its
  implementation; TOML-versus-JSON customization is isolated by facet, and all
  handlers access the shared vocabulary through the TOML namespace;
- `ArrayFmt` defaults to ordinary inline arrays, `Tables` produces conventional
  `[[...]]` output for every supported root/nested vector-of-object case, and
  invalid structural combinations fail at compile time with a focused
  diagnostic;
- the exhaustive `ScalarFmt` matrix covers every enum/default/alias across all
  applicable scalar types, collection contexts, and save filters; each explicit
  TOML string style round-trips representable values exactly, invalid
  property/type combinations fail at compile time, and every unrepresentable
  strict literal case fails with the focused save diagnostic;
- the dual-facet fixture proves independent JSON/TOML IDs and formatting with
  shared property types, and ordinary TOML output contains no JSON/Cf
  collection syntax;
- the completed red/amber audit records allocation paths and measured layouts,
  and leaves no anonymous namespace, typed enum, non-specific lambda capture,
  STL container, unnamed heap allocation, hidden temporary copy, or oversized
  parser frame in the new implementation;
- sanitizer and valgrind runs show no leaks, invalid access, or ownership
  cycles;
- review compares new files side by side with Cf/YAML for naming, layout,
  indentation, heap IDs, exception style, allocation behavior, and unnecessary
  divergence.

## Open questions

None at present. `ScalarFmt` and `ArrayFmt` make scalar spelling and array layout
explicit per-field output choices rather than global emitter policies or
heuristics; neither restricts valid TOML input.
