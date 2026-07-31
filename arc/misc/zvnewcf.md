# ZvNewCf completion plan

## Required end state

`ZvNewCf` is a thin Cf scanner and Cf-node loader layered on `ZtJSON`. Shared JSON
formatting, scalar conversion, delimiter, and save code must exist only in
`ZtJSON`; `ZvNewCf` imports it with the existing namespace directives:

```c++
namespace ZvNewCf { using namespace ZtJSON; }
namespace ZuFieldProp::Cf { using namespace ZuFieldProp::JSON; }
```

The completed implementation must have these properties:

- `scan()` accepts `ZuCSpan`; no scan function accepts or constructs a mutable
  span over the source.
- The returned tree owns every key and scalar value. No span in
  the tree points into the scanned input.
- The source may be read-only, unmapped, overwritten, or freed immediately
  after `scan()` returns.
- Text is copied once, directly from the source or environment into its final
  node storage. There is no intermediate heap string.
- A leading unquoted `%` in key position invokes an extensible percent
  directive. Directive expansion recursively parses directly into the current
  object.
- `ZvNewCf` does not contain namespace-renamed copies of reusable `ZtJSON` code.

## 1. Delete code that must be reused from ZtJSON

Perform this deletion before changing the Cf parser so the remaining surface is
unambiguous.

Delete these declarations/definitions from `ZvNewCf` and let unqualified lookup
resolve them through `using namespace ZtJSON`:

- `isspace__`
- `bod`
- `quote`
- `eov_Null`
- `eov_Number`
- `eov_Decimal`
- `eov_Float`
- `eov_True`
- `eov_False`

Delete the duplicated save implementation:

- the `saveField` and `saveValue` forward declarations
- `saveValue_`, `saveValue`, and `saveField`
- the Cf `QuoteBuf`

Retain the Cf handler types because they load `ZvNewCf::AnyNode`, not
`ZtJSON::AnyNode`, but replace their duplicated save bodies with delegation:

- `ZvNewCf::AsObject::Handler::save` calls the corresponding
  `ZtJSON::AsObject::Handler<O, Facet>::save<Filter>`.
- `ZvNewCf::AsArray::Handler::save` calls the corresponding
  `ZtJSON::AsArray<ElemCode, ElemProps>::Handler<O, Facet>::save<Filter>`.
- `ZvNewCf::AsStringDeflt::Handler::save` calls
  `ZtJSON::AsStringDeflt::Handler<O>::save`.

Retain `ZvNewCf_Fmt`, `ZvNewCf_StringFmt`, `AsObject`, `AsArray`, `AsString`,
`LoadVec`, `loadValue_`, and `loadValue`: their load paths are bound to the Cf
node type or Cf customization hooks. Do not copy a JSON function merely to
change its namespace. If a retained function contains an unchanged block of
JSON logic, first try to invoke the JSON helper for that block.

## 2. Use the existing AnyNode string storage

Do not introduce `ZtString` as node storage. The current `AnyNode` is already
prepared for owned text:

```c++
using StringBase = ZtString<
  ZtStringBuiltin<StringSize, ZtStringHeapID_<Node_HeapID>>>;
ZuDerive(String, StringBase);
```

Use `SNodeSize`, `StringSize`, `StringBase`, and `String` as-is. Do not
change their inline size, heap ID, container, or allocation policy.

Store each decoded key directly in its field:

```c++
ZuDerive(Field, (ZuTuple<String, ZuPtr<AnyNode>>));
```

`Object` is the original built-in array of `Field`; do not add a parallel key
array. Decode a key into a local `String` and move it directly into the
corresponding `Field`. Update field matching to pass `fields[i].p<0>()`
directly to `ZuMatcher`, relying on Z array/span interoperability.

Build every decoded scalar in `AnyNode::String`; the scanner has no field
schema and must not assign numeric or boolean types. `ZtString` maintains its
own terminator; `loadValue_<CString>` returns `data()` directly and performs no
allocation, append, or copy.

## 3. Replace the scanner API

Expose only immutable scanner signatures:

```c++
using PctFn = ZmFn<void(
  ZuCSpan, ZuSpan<const ZuCSpan>, ZmFn<void(ZuCSpan)>)>;
ZvExtern ZuTuple<int, ZuPtr<const AnyNode>> scan(
  ZuCSpan, PctFn pctFn = {}, ZmRef<Defines> defines = new Defines());
template <bool Key>
ZvExtern int eos(ZuCSpan, AnyNode::String &);
ZvExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Array(ZuCSpan);
ZvExtern ZuTuple<int, ZuPtr<AnyNode>> eov_Object(
  ZuCSpan, bool root = false);
```

`eos` returns the consumed input count and `-1` on error. It starts at the
first byte of the token, including an opening quote. Delete `bos` and `eok`;
keys use `eos<true>` and values use `eos<false>`. Every test of `Key` within the
implementation must use `if constexpr`. The Cf-specific `bok` only locates a
key after whitespace and rejects object delimiters; it does not scan the key.

`bov` remains Cf-specific. It skips leading whitespace and returns the offset
of the first value byte plus a Cf value type. It recognizes `[` and `{`
immediately. For a scalar it examines the complete unquoted token before
classifying it:

- every scalar, including `null`, selects `String`.

Do not classify scalar text. `null`, `true`, `false`, `42`, `trueValue`,
`nullPath`, `nanosecond`, and `123abc` are strings. Their interpretation depends
on the subsequent `ZtStruct` field load. Quoted and expanded values are likewise
strings.

## 4. Implement eos as one state machine

Implement one file-scope state machine used for keys and values. Do not use
regular expressions and do not create separate bare/single/double decoders.
Use the states `Bare`, `Single`, and `Double` plus an `escaped` transition.

The exact transitions are:

1. Start in `Bare` with an empty destination.
2. In `Bare`, an unescaped `'` enters `Single`; an unescaped `"` enters
   `Double`. Neither quote is copied.
3. In `Single`, an unescaped `'` returns to `Bare`. `${...}` is copied literally
   and is never expanded.
4. In `Double`, an unescaped `"` returns to `Bare`.
5. In every state, `\` consumes the following byte and decodes the existing
   JSON escapes (`\b`, `\f`, `\n`, `\r`, `\t`, escaped quotes/slash/backslash,
   and `\uXXXX`). Preserve surrogate-pair validation and write UTF-8 directly
   to the destination. A dangling escape or invalid Unicode sequence fails.
6. An unquoted delimiter ends the token only while in `Bare`. `eos<true>`
   delimiters are whitespace and `:`. `eos<false>` delimiters are whitespace,
   `,`, `)`, `]`, and `}`. The delimiter is not consumed by `eos`.
7. End of input ends a token only in `Bare`; end of input in `Single` or
   `Double` fails.
8. Empty quoted segments are valid. A token must contain at least one bare byte
   or one quoted segment.

Expansion is handled inline by this same loop:

- Recognize only `${name}` in `Bare` and `Double` `eos<false>` states.
- Accept names matching `[A-Za-z_][A-Za-z0-9_]*`.
- Reject `${}`, malformed names, and a missing `}`.
- Copy the name into a `ZtLocalString` solely to supply a terminated argument
  to `::getenv`; this is stack-first scratch with heap fallback.
- Append the environment value directly to `AnyNode::String`.
- Before consulting the environment, consult the `Defines` passed to `scan`;
  a definition overrides the environment.
- Expand an unset variable to an empty string.
- Never expand keys or text in `Single` state.

Do not reserve source length unconditionally. For a plain token with no quote,
escape, or `$`, assign the source span directly to the destination once. For
the slow path, append directly to the final `AnyNode::String`; do not decode to
scratch and copy afterward.

## 5. Rewrite array, object, and root parsing

Factor scalar construction into one file-scope helper used by both arrays and
objects. It receives the type from `bov` and does exactly one of the following:

- creates a `String` node first, passes its data to `eos`, and retains that same
  node as the result. Null-, numeric-, and boolean-looking lexemes all take this
  path.

Do not default-construct a placeholder node and replace it after determining
the type.

`eov_Array` keeps the current trailing-comma behavior, but takes `ZuCSpan` and
uses the shared scalar helper. Use the imported `bod<']'>` for whitespace and
delimiter handling rather than a general whitespace-skipping helper. Nested
arrays and objects recurse without making the source mutable.

`eov_Object` has two modes:

- `root == false`: the caller has consumed `{`; parsing must consume `}`.
- `root == true`: parsing begins with the first key and end of input is the
  implicit closing brace. For compatibility with delegated JSON save output,
  this mode may also consume one optional leading `{` and then require `}`.

For each field, construct an owned `AnyNode::String` key, call
`eos<true>(span, key)`, consume optional whitespace and `:`, parse the
value, retain the key storage in the object, and push a `Field` spanning that
owned storage. Never assign a source span to a field. Use imported `boc` and
`bod<'}'>` for colon and explicit-object delimiter handling. The implicit root
uses `eor` to recognize a comma or whitespace-only end of input. Do not add a
general `skipSpace`. Preserve skipped fields and trailing
commas only when the existing JSON grammar accepts them without consuming
non-delimiter text.

When `bok` encounters a leading unquoted `%`, parse a directive rather than a
field. The syntax is `%directive(args...)`, where `directive` matches
`[A-Za-z_][A-Za-z0-9_]*` and each comma-separated argument is decoded with
`eos<false>`. Quoted or escaped leading `%` remains an ordinary key.

The built-in `%define(key, value)` requires exactly two arguments, copies them
into the `Defines` passed to `scan`, and produces no fields. The same instance
is carried through nested values and recursive percent expansion. Definitions
persist across scans only when the caller explicitly passes the same
`ZmRef<Defines>` to each scan. Other directives require `PctFn`. Invoke it
synchronously with the directive, argument spans, and an expansion callback.
If the application calls the expansion callback, recursively parse the
supplied span directly into the current `Object`; do not build a temporary tree
and splice it afterward. The expansion callback is valid only during the
`PctFn` call. A directive that does not call it produces no fields. Reject an
unknown directive when `PctFn` is empty and propagate a recursive expansion
parse failure through `scan`.

Delete `botl`. `scan` performs only these operations:

1. Reject a null span; accept an empty or whitespace-only span as an empty root
   object.
2. Parse directly into the root object, carrying `PctFn` and the passed
   `Defines` through recursive array/object parsing.
3. Skip trailing whitespace.
4. Fail if any other input remains.
5. Return the consumed byte count and the object node.

A top-level array is invalid. Nested arrays remain valid values.

## 6. Adapt the retained load code

Keep the current Cf `AsObject`, `AsArray`, `AsString`, `LoadVec`, and
`loadValue` structure, with these exact edits:

- Match an object field using its owned `String`, not a `ZuCSpan` into input.
- Return `AnyNode::String::data()` directly for `CString`.
- Convert owned `String` arrays to spans through implicit Z conversions; add no
  casts.
- Interpret boolean and numeric text only after field matching, according to
  the `ZtStruct` field type and properties.
- Decode bytes directly into the returned byte container. Do not mutate the
  node-owned encoded string and do not retain the JSON in-place idempotence
  markers; those exist only because JSON decodes within its scanned buffer.
- Continue using imported `eov_Decimal` and `eov_Float` for numeric/time loads.
- Keep `LoadVec` as the current thin `ZuMArray` wrapper; add no scratch array or
  ownership side channel.

The public `save`, `saveUpd`, and `saveDel` entry points continue to dispatch
through the selected Cf handler. Their handler save functions delegate to JSON
as specified in section 1. The scanner accepts the resulting explicit-brace
object as well as the canonical implicit-root input.

## 7. Build integration and tests

Add `ZvNewCf.hh` to `zv/src`'s `pkginclude_HEADERS`, add `ZvNewCf.cc` to
`libZv_la_SOURCES`, and add `ZvNewCfTest` to `zv/test/Makefile.am` with a separate
`ZvNewCfTest_SOURCES` entry.  `ZvNewCf` lives in `zv` because its contextual load
errors use `ZeString` and `ZeEXCEPT`.

`ZvNewCfTest.cc` must contain these groups:

1. **Ownership:** scan a `static const char[]`; scan a scoped `ZtString`, destroy
   it, then verify every key/scalar and load a reflected object while
   retaining only the tree.
2. **Quoting:** bare, single, double, empty quotes, adjacent quoted segments,
   `foo'bar'"baz"'bam'`, escaped quotes/delimiters, all simple escapes, BMP
   Unicode, a surrogate pair, dangling escape, invalid Unicode, and each
   unterminated quote form.
3. **Expansion:** set one test variable to `X` and verify the supplied
   `foo'bar'"baz${x}bah"'bam' -> foobarbazXbahbam` case; also test bare,
   double, single/no-expansion, adjacent expansions, unset variables, empty
   names, invalid names, and missing braces. Save and restore the original
   process environment.
4. **Classification:** verify that null, true/false, integer, fraction,
   exponent, signed numbers, `trueValue`, `nullPath`, `nanosecond`, `123abc`,
   quoted numbers, and expanded numeric text are all `String` nodes.
5. **Grammar:** `x: 42, y: 43`, no whitespace around `:`, mixed-quoted keys,
   empty root, explicit root braces, nested objects/arrays, trailing commas,
   skipped fields, invalid separators, unexpected closers, trailing garbage,
   and rejected top-level arrays.
6. **Percent directives:** built-in definition, redefinition, definition over
   environment precedence, decoded arguments, quoted/escaped percent keys,
   custom no-expansion directives, recursive expansion into the current root
   and nested objects, nested directives, and malformed syntax.
7. **Load/save:** strings, `CString`, bytes and byte vectors, all numeric forms,
   enum/flags, time, nested UDT, vectors, optional fields, ctor/load/update, and
   `save -> scan -> handler` round trips.
8. **Immutability/repeated load:** retain a byte-for-byte copy of source input
   and compare it after scan and after two loads of the same encoded bytes
   field; both loads must return the same decoded bytes without changing the
   encoded node string.

Use `ZuTestUtil`; do not add ad hoc output parsing or fixed-capacity test
buffers.

## 8. Acceptance sequence

1. Inspect `git diff -- zv/src/ZvNewCf.hh zv/src/ZvNewCf.cc` and verify that no
   reusable function listed in section 1 remains duplicated.
2. Run `git diff --check`.
3. Run top-level `make -j8`.
4. Run `./zv/test/ZvNewCfTest`.
5. Run `make -C zt/test -j8 && make -C zt/test test`.
6. Run top-level `make test`.
7. Review the final diff against `GUIDELINES.md` for mutable source spans,
   source-backed node data, unnamed allocation, temporary strings, repeated
   copies, unnecessary casts, fixed scratch buffers, and default-then-replace
   node construction. None may remain.
