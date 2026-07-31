# ZtStruct input-validation audit

This is a static audit of the current working tree.  It distinguishes four
layers which are otherwise easy to conflate:

1. wire or text syntax validation;
2. binding parsed values to `ZtStruct` fields;
3. scalar conversion and domain validation (`Required`, inclusive `Range`,
   enum and flags membership); and
4. propagation of a useful error to the caller.

The intended general `ZtStruct` binding contract is: an absent field selects
the field default, while a present explicit null or a field-level conversion
parse failure selects the destination type's sentinel null.  A malformed
whole input document is different: the format scanner fails and no
successfully bound object result should be inferred.  The audit below records
where each adapter implements or departs from that contract.

The result terms below are deliberately distinct:

- **field default** means `Field::deflt()` for a scalar field;
- **sentinel null** means the type's logical null (`ZuCmp<T>::null()`, a null
  pointer, or the corresponding default-null value), irrespective of the
  configured field default; and
- **empty vector** means the adapter's empty vector/view result, which is not
  a configured scalar default.

A **syntax failure** happens before field binding and makes the format scanner
fail.  A **conversion failure** is a syntactically valid, present field whose
node type or contents cannot be converted to the `ZtStruct` field type.  Only
JSON has a typed explicit-null scalar token; the other formats' closest cases
are called out rather than being described loosely as “null/default”.

All current `ZtFieldScanInt` dependents share the same integer-range contract.
`ZtJSON`, `ZvNewCf`, `ZtCSV`, and `ZtURI` call the common scanner directly, while
`ZtCLI` binds values through `ZtURI`.  After decimal, hexadecimal, enum, or
flags scanning, an explicit inclusive `Range` is checked; an out-of-range
value is a conversion failure and is coerced to the destination integer type's
sentinel null.  `ZtJSON` applies the same bounds explicitly to native JSON
number nodes, which do not pass through the textual scanner.

## Findings

| Adapter | Syntax / wire validation | Missing, explicit null, and conversion result | Scalar and domain validation | Unknown and repeated input | Error propagation |
|---|---|---|---|---|---|
| `ZtJSON` | Validates JSON structure and typed JSON tokens while building the node tree. | Missing scalar: field default. Missing vector: empty vector. Explicit JSON `null`: sentinel null for a scalar, empty vector for a vector. Present wrong-type, unscannable, or out-of-`Range` numeric value: sentinel null. A UDT node whose shape does not match its selected object/array/string formatter also returns `ZuCmp<T>::null()`; a valid empty object still constructs the UDT from member defaults. Invalid byte encodings are not reliably diagnosed. `Required` is not enforced. | Inclusive `Range` is enforced for integer, float, fixed, and decimal fields, for both JSON number nodes and string-formatted values. Enum/flags names are checked on the string path, but a JSON number node bypasses those symbolic domains. | Unknown object members are retained by the parser and ignored by object loading. No adapter-level duplicate-field diagnostic was found. | Invalid JSON makes the scanner fail. A syntactically valid field conversion failure returns the result described at left, without a field-path error. |
| `ZvNewCf` | Validates CF structure and directives, using `ZtJSON`'s parser machinery. CF scalar values are string nodes, so JSON token typing does not apply. | Missing scalar: field default. Missing vector: empty vector. There is no typed null scalar; `null` is the string `"null"`. Wrong shape: `CString` becomes `nullptr`; string, bytes, integer, float, and time take their type sentinel; fixed/decimal return `T{}`; a standard object/array/string-formatted UDT returns `ZuCmp<T>::null()` when its node shape does not match. A valid empty object constructs a UDT from member defaults, while an `AsString` UDT receives `null` as the literal string. Bad or out-of-`Range` numeric text has the same sentinel result; invalid Boolean becomes `false`, and byte codecs can return decoded/partial output. `Required` is not enforced. | Inclusive `Range` is enforced for integer, float, fixed, and decimal fields; integer enum/flags values are checked after their symbolic scan. Several textual conversions accept a valid prefix without requiring full-span consumption. | Unknown object members are ignored by typed loading. Directive errors can propagate through the boolean `PctFn`/expansion callbacks. | Structural/directive failure makes `scan()` fail. Present-field conversion returns the value described at left and carries no field path. |
| current `ZvCf` | Delegates CF parsing, then performs its own getter-based conversion. | Missing optional scalar: field default. Missing vector: default-constructed container. Missing `Required` field: exception. There is no typed explicit null. Bad Boolean, enum, flags, or out-of-range scalar text throws. A failed integer scan produces the integer sentinel, which is outside the permitted range and therefore throws; a failed floating scan produces NaN, whose ordered min/max comparisons are both false, so NaN is returned. Time/UDT text is returned through `T(s)` without checking whether the constructed value is null; failed numeric vector-element scans return sentinel elements. | Generic scalar getters add strict Boolean, inclusive min/max, enum and flags checks. Numeric scanning can accept a valid prefix; fixed/decimal null handling follows their comparison operators rather than an explicit scan-success check. | Getter lookup selects named values; unknown input is not rejected globally. | Checked getter failures throw `ZeException` with the configuration path. The unchecked constructor/vector paths return their constructed value or sentinel element without throwing. These checks belong to the legacy getter layer. |
| `ZtASN1` | `loadTL()` checks expected tags and bounds encoded lengths, but the loader does not establish canonical DER, complete top-level consumption, or a persistent success state. | Missing or mismatched object field: field default for a scalar, empty vector for a vector. ASN.1 has no generic explicit-null value here. A present zero-length object scalar is also treated as missing and therefore gets the field default. Direct or sequence-element zero-length Boolean/Integer decoding returns `ZuCmp<T>::null()`. Malformed REAL-to-float returns NaN; malformed REAL-to-fixed/decimal and unsupported fixed/decimal mappings return `T{}`. Text integer/float parse failures return `ZuCmp<T>::null()`; text fixed/decimal failures return `T{}`. `Required` is not enforced. | Does not apply min/max or enum/flags domain checks. Integer decoding does not reject values wider than the destination. | Fields are consumed in schema order. Unexpected/mismatched and trailing TLVs are not reported as unknown fields or duplicates. | `LoadContext` returns early or skips a field on failure, but `Handler` exposes no failure result; structural corruption can therefore be observed as a missing field and receive its field default. |
| `ZtCLI` | Rejects malformed keys, unknown short/long options (including `--name=value`), missing named-option values, and conflicting object/array shapes. | Missing scalar option/argument: field default. Missing vector: empty vector. There is no null token. A bare flag or Boolean option means `true`; invalid explicit Boolean text becomes `false`. Other bad or wrong-shaped values use `ZtURI`: wrong-shaped `CString` becomes null and `String` becomes empty; bytes, integer, float, and time return `ZuCmp<T>::null()`; fixed/decimal return `T{}`; a UDT gets member defaults. An out-of-`Range` numeric value also returns its sentinel. `Required` and positional arity are not enforced from field metadata. | Through `ZtURI`, inclusive `Range` is enforced for integer, float, fixed, and decimal fields; enum/flags symbolic scanning applies to integers. | Unknown options are rejected. Repeated scalar keys become arrays; binding that wrong shape follows the per-type rules at left rather than reporting a duplicate. | Scanning returns `bool`; `load()` returns `-1` on parse failure or positional `argc` on success. Conversion/binding has no field-path diagnostic. |
| `ZtCSV` | The streaming splitter validates CSV quoting and row/header framing. It does not enforce a typed schema or exact row width. | Missing header mapping: field default for a scalar, empty vector for a vector. CSV has no null token. An explicit empty cell is empty for `CString`/`String`; bytes, integer, float, and time return their type sentinel; fixed/decimal return `T{}`. An out-of-`Range` numeric value likewise returns its sentinel. Boolean scans through `ZuBox<uint8_t>`: failed/empty input yields the `uint8_t` sentinel `255`, then converts to field type `T` (`true` when `T` is `bool`, `255` for a wider integral `T`); successfully scanned input is likewise converted from its numeric `uint8_t` value. `Required` is not enforced. | Inclusive `Range` is enforced for integer, float, fixed, and decimal fields; enum/flags symbolic scanning applies to integers. Byte decoders can return decoded/partial output without a conversion error. | Unknown columns are ignored. A repeated known header name overwrites its earlier lookup entry. Scalar/array cell forms are coerced rather than rejected. | Structural splitting returns a byte count or `-1`; typed field loading returns the per-type result at left with no row/column diagnostic. |
| `ZtURI` | Rejects malformed percent escapes, malformed member paths, missing separators, and incompatible node shapes while parsing. | Missing scalar: field default. Missing vector: empty vector. There is no null token. A present empty string remains empty for `CString`/`String` and returns the type sentinel for bytes, Boolean, integer, float, and time (`T{}` for fixed/decimal). A wrong-shaped scalar is converted from a default empty span: `CString` becomes null, `String` empty, and other scalar types take the same sentinel/`T{}` results; an out-of-`Range` numeric value also takes its sentinel, while a wrong-shaped UDT gets member defaults. `Required` is not enforced. | Boolean spellings are exact. Inclusive `Range` is enforced for integer, float, fixed, and decimal fields; enum/flags symbolic scanning applies to integers. Several conversions do not require full-span consumption. | Unknown keys are retained then ignored. Repeated strings become arrays; a repeated scalar consequently follows the wrong-shape results at left instead of producing a duplicate error. | Syntax/tree-construction failure is `-1`; typed conversion returns the per-type result at left without a path or reason. |
| `ZfbStruct` | `verify()` uses the FlatBuffers verifier for buffer bounds, offsets, vtables, strings and vectors. Validation is optional: `root()` and `ctor()` do not call it. | An absent scalar yields the FlatBuffers schema default, not `Field::deflt()`. FlatBuffers has no separate explicit-null scalar. An absent/null string or byte-vector offset becomes a null span; other absent/null vector or nested offsets become that transformer's `T{}` result. The wire format does not distinguish omission from null offset. `ZtFieldProp::Required` is not enforced unless independently represented as FlatBuffers `(required)`. | Generated accessors enforce wire representation, not application domains. Min/max and enum/flags membership are not checked; enum storage can contain undeclared values. | A table cannot contain duplicate field instances. Unknown future table fields are ignored by an older generated reader. | Failed `verify()` returns null and no object should be bound. Once construction starts there is no conversion-error channel; callers that skip `verify()` rely on the buffer already being valid. |

## Detailed observations

### `ZtJSON`, `ZvNewCf`, and current `ZvCf`

`ZtJSON` validates the JSON representation, including whether a node is an
object, array, string, number, Boolean, or null.  That validation ends at the
representation boundary.  Its object handler (`zt/src/ZtJSON.hh`) supplies
`Field::deflt()` only for an absent scalar field.  A present JSON `null`
reaches `loadValue_()` and returns `ZuCmp<T>::null()` instead; it does not
select the configured field default.  Missing and explicitly null vectors
both produce an empty vector view.  A wrong-type scalar also returns its
sentinel.  UDT loading checks the selected standard formatter's node shape:
object-, array-, and string-formatted UDTs reject other node types with
`ZuCmp<T>::null()`.  A valid empty object is different: it is an object of the
right shape, so its absent members receive their own field defaults.  The
handler does not inspect `Required`.  Integer loading enforces an explicit
inclusive `Range`: string-formatted integers use `ZtFieldScanInt`, with an
out-of-range result coerced to the integer sentinel, and JSON number nodes
apply the same bounds after decimal scanning.  The latter path
still permits fractional/exponent JSON numbers and floors them as before.
Float, fixed, and decimal values are likewise checked after conversion, for
both string and native number nodes.

`ZvNewCf` is based on the same node/parser architecture, but CF scalar input is
represented as a string.  It therefore benefits from structural parsing, not
from JSON's typed scalar-token validation.  `ZvNewCf::AsObject` follows the same
field-default-on-absence rule as `ZtJSON::AsObject`, but CF has no typed null
scalar: the text `null` is ordinary scalar text.  The selected standard UDT
formatter must match that node's shape; an object-formatted UDT therefore
rejects both `42` and `null` as strings with `ZuCmp<T>::null()`, while an
`AsString` UDT receives `null` as literal input.  A valid empty object still
constructs the UDT from its member defaults.  Failed scalar conversion
produces the integer/float/fixed/decimal/time sentinel or default-null value,
not `Field::deflt()`; invalid Boolean text becomes `false`, while byte codecs
can return decoded/partial output.  Missing and wrong-shaped vectors become
empty vectors.  Integer fields use `ZtFieldScanInt`, so an explicit inclusive
`Range` is checked after ordinary, hexadecimal, enum, or flags scanning.  The
out-of-range result is the destination integer sentinel.  For an in-range
scan, the returned byte count remains the consumed prefix length; trailing
text is not itself a range or scan error.  Float, fixed, and decimal values
are also checked after conversion.

The stricter behaviour attributed to configuration loading currently comes
from `ZvCf`'s generic getter implementation.  That layer explicitly checks
required fields, strict Booleans, numeric bounds, and enum/flags domains and
adds the resolved configuration path to its exception.  Deleting those
getters without moving the desired checks would delete that behaviour;
deriving the new thin wrapper from `ZvNewCf` does not preserve it implicitly.
For optional scalar fields, those getters select `Field::deflt()` when lookup
finds no usable value.  Bad Boolean, enum, flags, and range values throw, but
a failed integer scan yields the reserved integer sentinel and then fails the
range check.  A failed floating scan yields NaN; both ordered range comparisons
with NaN are false, so the NaN is returned.  Time/UDT constructors, vector
elements, and fixed/decimal null values have no common scan-success check.

### `ZtASN1`

`loadLen()` and `loadTL()` (`zt/src/ZtASN1.hh:529`) prevent an encoded content
length from extending beyond the supplied span and compare the expected tag.
They are traversal helpers rather than a complete DER verifier: non-canonical
length encodings and unconsumed top-level data are not rejected.

`LoadContext` (`zt/src/ZtASN1.hh:624`) records spans for fields it recognizes.
On a top-level error it returns from its constructor; on many field-level
mismatches it leaves the field empty or skips the enclosing nested value.
`AsObject::Handler::loadField()` (`zt/src/ZtASN1.hh:810`) cannot distinguish
that state from an absent optional field: a scalar receives `Field::deflt()`
and a vector receives an empty view.

The primitive loader also assumes more than the TLV traversal establishes.
DER requires Boolean and Integer content to contain at least one octet, but
`loadTL()` accepts a length of zero because its checks only establish that the
declared content fits inside the input span.  The subsequent behaviour depends
on the call path:

- For an ordinary object field, `LoadContext` stores a zero-length span and
  `AsObject::Handler::loadField()` tests that span as false.  It does not call
  the primitive decoder; it silently returns `Field::deflt()`.  Malformed input
  is therefore indistinguishable from an absent optional field.
- For an element of `SEQUENCE OF`, `LoadVec` pushes the zero-length span into
  its underlying array.  Its lazy `get()` later calls `loadValue()` with that
  span.  Boolean and Integer decoding now check the length and return
  `ZuCmp<T>::null()` for the malformed element.  A direct call to the public
  `loadValue()` helper has the same sentinel result.

Thus the normal scalar-object path does not itself dereference the empty span;
it masks the malformed value as missing and selects the field default, whereas
collection-element and direct primitive decoding select the type sentinel.
Separately, an Integer of arbitrary encoded width is shifted into the
destination type without a range/width error.

### `ZtCLI`

`Parser::scanArg()` (`zt/src/ZtCLI.hh:1125`) validates both bare long options
and `--name=value` against the declared CLI fields using `longOptType_()`.
Unknown names are rejected before they are added to the node tree.  The lookup
includes save/constructor fields so read-only constructor options emitted by
CLI serialization remain valid input.

The parser deliberately promotes repeated scalar nodes to arrays through
`ZtURI::string()`.  This is useful for vector-valued options, but a scalar
field presented repeatedly is not diagnosed as a duplicate: scalar loading
receives the wrong node shape.  URI conversion then sees an empty span: a
`CString` becomes null, `String` becomes empty, numeric/bytes/time types become
their sentinel or `T{}` result, and a nested UDT is built from member defaults.

The object handler (`zt/src/ZtCLI.hh:810`) gives an absent scalar its field
default and an absent vector an empty view; it does not enforce `Required`.
CLI has no null token.  A bare flag or Boolean option binds `true`.  An
explicit Boolean string is processed with non-strict `ZtScanBool`, so bad text
binds `false`; other values use the `ZtURI` rules described below, including
inclusive numeric `Range` checking and sentinel-null coercion on failure.

The parser does not infer a required positional arity from `CLI::Arg<N>` or
`Required`: an absent mapped position defaults like any other absent field.
This is normally enforced one layer up.  `ZtCLI::load()` returns `-1` for a
parse failure and otherwise returns the parsed positional `argc`; most
dependent commands compare that count with the command's expected arity and
emit usage on mismatch.

### `ZtCSV`

`split()` (`zt/src/ZtCSV.cc`) is a structural streaming parser.  The typed
reader (`zt/src/ZtCSV.hh:816`) maps header positions to fields, ignores unknown
columns, and gives a scalar with no mapped column `Field::deflt()` (or an empty
vector for a vector field).  It does not validate `Required` or require the
number of cells in every row to equal the header.  CSV has no typed null: an
empty cell is a present empty string, not a missing field.

The primitive loader (`zt/src/ZtCSV.hh:392`) unquotes and converts in place.
Its Boolean representation is numeric, and the numeric, enum, flags, date and
byte-codec paths do not report conversion or domain failures to the reader.
String fields preserve an empty cell as an empty string; bytes and most
numeric/date fields return their sentinel for empty or unscannable text.  The
Boolean path instead converts a `ZuBox<uint8_t>` result to field type `T`.
Failed or empty input produces the `uint8_t` sentinel `255`, which becomes
`true` for `bool` or `255` for a wider integral type.
Successfully scanned text is converted from the scanned `uint8_t` value in the
same way.  Cell arrays are also intentionally coerced: scalar loading uses one
value and vector loading can wrap a scalar.  Integer conversion uses
`ZtFieldScanInt`, including inclusive `Range` and symbolic enum/flags checks;
float, fixed, and decimal values are range-checked after conversion.  Parse or
range failure becomes the destination sentinel and is not reported to the
reader.

### `ZtURI`

URI scanning validates percent escapes and incrementally builds a typed
object/array/string tree.  Tree-shape conflicts fail during parsing.  Once the
tree exists, however, `AsObject::Handler::loadField()`
(`zt/src/ZtURI.hh:1574`) gives a missing scalar `Field::deflt()` and a missing
vector an empty view.  URI has no null token.  A present empty string remains
empty for `CString`/`String` but produces a sentinel for bytes, Boolean,
integer, float, and time fields (`T{}` for fixed/decimal).  A scalar node with
an incompatible shape is fed a default empty span: `CString` becomes null,
`String` remains empty, and the other scalar results are unchanged.  A
wrong-shaped UDT is constructed from member defaults.  The handler does not
inspect `Required`; numeric conversion enforces an explicit inclusive `Range`,
coercing parse and range failures to the destination sentinel.  Enum/flags
formatting applies to integer conversion.

The Boolean converter (`zt/src/ZtURI.hh:1395`) is stricter than the CF/CLI
Boolean paths because it enumerates the exact accepted spellings.  Its failure
result is still only the type's null sentinel.  Repeated keys are promoted to
arrays by `string()` (`zt/src/ZtURI.hh:516`), supporting vectors but silently
changing scalar binding to the wrong-shape results above.

### `ZfbStruct`

`ZfbStruct::verify()` (`zfb/src/ZfbStruct.hh:304`) provides the strongest wire
integrity check in this set, but it is a separate opt-in entry point.
`ZfbStruct::root()` directly calls `GetRoot`, and `ctor()` immediately invokes
generated field accessors.  Existing code therefore has to preserve the
invariant that unverified buffers never reach those paths.

`Handler_` (`zfb/src/ZfbStruct.hh:173`) maps generated FlatBuffers accessors
straight into constructor/setter arguments.  It does not reconcile
FlatBuffers scalar defaults with `Field::deflt()` and does not apply ZtStruct
required, range, enum, or flags validation.  FlatBuffers `(required)` is a
separate schema property and cannot be inferred from `ZtFieldProp::Required`
by this reader.  Generated scalar accessors return the FlatBuffers schema
default when a field is absent.  Offset accessors return null for an absent
offset.  String and byte-vector transformers turn it into a null span; other
vector and nested transformers return their `T{}` result.  The wire format
does not preserve a separate omitted versus explicit-null distinction for that
offset.

## Cross-adapter conclusions

- Structural validation is generally present and format-specific.  It should
  not be cited as evidence that `ZtStruct` metadata is enforced.
- `Required` is not generically enforced by the audited `AsObject`/`Handler_`
  adapters.  Every current `ZtFieldScanInt` dependent enforces an explicit
  inclusive integer `Range`: JSON, CF, CSV, and URI call it directly, and CLI
  binds through URI.  Parse and range failures are coerced to the destination
  integer sentinel.  JSON additionally checks native, unquoted number nodes
  because that path bypasses `ZtFieldScanInt`.  JSON, CF, CSV, and URI also
  enforce inclusive `Range` for float, fixed, and decimal values after
  conversion; CLI inherits that behavior from URI.  Current `ZvCf` adds
  broader application-layer checks through its legacy getters.
- Missing scalar input selects `Field::deflt()` in the JSON, CF, CLI, CSV,
  URI, and ASN.1 object handlers.  Present-but-invalid input follows the
  per-type results in the table instead of selecting that field default.  The
  important departures are ASN.1 structural/zero-length object fields (masked
  as missing and therefore defaulted), permissive Boolean paths, URI/CLI UDT
  fallback, and FlatBuffers schema defaults.  None of these result
  distinctions supplies a field-path diagnostic.
- Duplicate/unknown-field policy is format-dependent: JSON/CF/URI retain
  unknown data, CLI rejects unknown options, CSV ignores unknown columns,
  ASN.1 does not surface unexpected TLVs, and FlatBuffers tables safely ignore
  unknown schema fields.
- If the new thin `ZvCf` is intended to retain the current validation contract,
  the contract needs an explicit checked `ZvNewCf` binding layer or an equivalent
  validation pass.  `ZtJSON` ancestry alone does not provide it.
