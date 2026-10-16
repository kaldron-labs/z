# `ZfYAML` / `ZvYAML` implementation prescription

This document is normative for the implementation. `Must`/`do` items are
requirements, not suggestions. Where it is silent, copy the corresponding
`ZfCf`/`ZvCf` code rather than inventing a different abstraction. Do not leave
parser architecture, ownership, limits, or `ZvYAML` file-loading structure as
implementation-time choices.

## Objective

Add `ZfYAML` beside `ZfCf` in `zf` and `ZvYAML` beside `ZvCf` in `zv`.
The new code must look like a format-specialized copy of the corresponding
Cf code: the same public layering, shared `ZfTree` representation and
`ZfTreeLoad` machinery, error style, allocation strategy, short parser names
(`bov`, `eov`, etc.), file I/O shape, and test organization. Every difference
must be traceable to YAML syntax or semantics, not to an unrelated redesign of
the Cf implementation.

Use YAML 1.2.2 as the language reference, but intentionally implement the
mainstream JSON-interoperable profile used by formats such as OpenAPI rather
than the complete YAML representation graph:

- <https://yaml.org/spec/1.2.2/>
- <https://spec.openapis.org/oas/latest.html#json-and-yaml-compatibility>
- <https://spec.openapis.org/oas/v2.0.html#format>
- <https://www.rfc-editor.org/rfc/rfc9512.html#section-4.4>
- <https://docs.docker.com/reference/compose-file/fragments/>
- <https://docs.gitlab.com/ci/yaml/yaml_optimization/>

## Scope contract

Implement a deliberately bounded, single-document YAML 1.2.2 configuration
profile that maps directly to the canonical `ZfTree` representation, shared
`ZfTreeLoad` model, and the JSON data model. The intended input is ordinary
OpenAPI-style YAML: mappings with string keys, sequences, and scalar leaves.
Every accepted document must have a straightforward JSON representation after
scalar decoding; accepting the full YAML graph model is explicitly not a goal.

The profile is:

- Support block mappings and sequences, including indentless sequences used as
  mapping values and compact mappings used as sequence entries; flow mappings
  and sequences; plain,
  single-quoted, double-quoted, literal (`|`), and folded (`>`) scalars,
  comments, and optional document start/end markers. These are the mainstream
  YAML presentation features used to express JSON-shaped data.
- Accept valid JSON object/array syntax as YAML flow input, including quoted
  JSON keys and JSON string escapes supported by YAML double-quoted scalars.
- Preserve decoded scalar content in owned `String` nodes and record a compact
  scalar kind (`String`, `Number`, `Bool`, or `Null`) so OpenAPI/JSON transforms
  can distinguish `1` from `"1"` and `null` from `"null"` without reparsing.
  `ZfStruct` field metadata still performs the actual conversion to C++ types.
- Apply JSON-compatible scalar recognition to unquoted plain scalars, extended
  by the standard YAML integer forms: YAML 1.2 `0o` octal and `0x` hexadecimal,
  plus YAML 1.1 binary, leading-zero octal, sexagesimal, leading `+`, and `_`
  digit separators. Also recognize `null` and both YAML 1.2 and YAML 1.1
  implicit booleans.
  Accept the YAML 1.1 boolean spellings
  `y|Y|yes|Yes|YES|n|N|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF`.
  They still resolve to the JSON boolean type, so they do not expand the node
  model beyond JSON. Quoted and block scalars with the same text remain strings.
  Do not add other YAML-only numeric spellings or implicit timestamp typing.
  Application-specific enums, flags, dates, and formatted numbers remain
  strings until their target `ZfStruct` field handlers parse them.
- OpenAPI/Swagger version detection does not alter this parser rule. Published
  OpenAPI specifications reference or recommend YAML 1.2 and JSON-compatible
  representation, but do not explicitly prohibit the YAML 1.1 boolean lexemes.
  If a future consuming specification makes them invalid, enforce that in its
  schema/validation layer rather than changing generic YAML parsing.
- Restrict mapping keys to scalar strings because JSON object keys are strings,
  `AnyNode::Object` stores `String` keys, and `ZfStruct` matching is string-ID
  based. Accept ordinary plain and quoted scalar keys and decode them as strings
  without implicit scalar typing. Reject aliases and anchors in key position,
  the explicit `?` key form, and sequence or mapping nodes used as keys.
- Require unique mapping keys. Cf's last-value-wins duplicate behavior is not
  carried over because YAML mappings require unique keys and duplicate JSON
  object names are not interoperable.
- Accept one document per `scan()`/`load()` call. Reject a second document and
  report `SecondDocument` at its marker. Treat a blank configuration stream as
  an empty mapping, preserving `ZfCf` configuration behavior and giving it the
  unambiguous JSON representation `{}`.
- Compose an omitted block mapping value (`key:`), an empty block sequence
  entry (`-`), and an omitted flow-mapping value (`{key:}`) as a `Null` scalar
  with empty stored text. Reject omitted flow-sequence entries such as
  `[a,,b]`; they are unnecessary for mainstream JSON-shaped configuration.
- Support anchors (`&name`) and aliases (`*name`) because they are mainstream in
  configuration YAML, including Docker Compose and GitLab CI. Compose them
  during parsing: an alias becomes a deep copy of the previously anchored node,
  anchor names are discarded, and the resulting `AnyNode` remains a
  JSON-interoperable uniquely owned tree. Support anchors on scalar, sequence,
  and mapping nodes; an alias may appear wherever the anchored node kind is
  valid.
- Require aliases to reference the most recent preceding anchor with that name,
  following YAML serialization semantics. Anchors are permitted only on value
  nodes and aliases are permitted only as complete value nodes. Reject forward
  references, direct or indirect cycles, and expansion beyond the fixed public
  alias/depth/node budgets specified below.
- Continue to reject tags, merge keys, explicit complex keys, sets, ordered-map
  extensions, and any cyclic/shared graph that cannot be composed into a finite
  JSON tree. Merge-key processing is a separate mapping transformation and is
  not implied by supporting `&`/`*`.
- Reject all `%` directives, including `%YAML` and `%TAG`. Directives are rare in
  mainstream configuration YAML, are not part of the represented data, and do
  not interoperate with JSON. An optional bare `---` document-start marker is
  sufficient for the supported single-document profile.
- Do not copy Cf-only `${...}` expansion, `%define(...)`, arbitrary `%...`
  callbacks, or `%include(...)` into YAML. They are not YAML syntax. `ZvYAML` is
  therefore the mmap-backed file load/save layer without recursive include
  expansion; include processing is outside this component's profile.
- Parse UTF-8 bytes without transcoding. Accept and consume one UTF-8 BOM only
  at byte zero; reject it elsewhere. Validate raw and escaped Unicode scalar
  values, normalize CRLF/CR/LF to LF in decoded multiline scalars, reject
  forbidden control characters, and reject tabs used for indentation. Keep
  ordinary valid non-ASCII UTF-8 bytes unchanged.

This scope keeps the implementation structurally parallel to `ZfCf`, accepts
the YAML normally seen in OpenAPI and mainstream configuration files, and
avoids obscure language features that do not round-trip through JSON. Describe
the implementation as a JSON-interoperable YAML configuration parser, not as a
fully conforming general-purpose YAML processor. The rejected cyclic-graph,
tag, directive, merge-key, and complex-key features are deliberate profile
boundaries, not missing conformance work to be added implicitly later.

## Files and build wiring

The `cftree.md` precursor is complete: `ZfTree.hh` is the canonical owned node
tree, `ZfTreeLoad.hh` is the policy-driven load engine, and current `ZfCf` is a
thin format facade over them. Treat these as existing dependencies. Do not copy
their definitions into YAML or refactor them again in this change.

Create these files by starting from the format-specific portions of their Cf
counterparts and mechanically renaming the component, namespace, facet,
format-specific heap IDs, exception macro, and test types before changing
grammar-specific code:

- `zf/src/ZfYAML.hh` from `zf/src/ZfCf.hh`
- `zf/src/ZfYAML.cc` from `zf/src/ZfCf.cc`
- `zf/test/ZfYAMLTest.cc` from `zf/test/ZfCfTest.cc`
- `zv/src/ZvYAML.hh` from `zv/src/ZvCf.hh`
- `zv/src/ZvYAML.cc` from `zv/src/ZvCf.cc`
- `zv/test/ZvYAMLTest.cc` from `zv/test/ZvCfTest.cc`

`ZfYAML.hh` starts from the current thin `ZfCf.hh` facade, not from a historical
version containing its own node or load implementation. `ZfYAML.cc` starts from
the `ZfCf.cc` scanner structure but replaces only the grammar-specific parser
body. Common node allocations retain the `"ZfTree.Node"` heap identity;
YAML-specific scanner, anchor-index, scratch, and callback storage uses
`ZfYAML.*` heap IDs.

Update:

- `zf/src/Makefile.am`: install `ZfYAML.hh` and compile `ZfYAML.cc` into
  `libZf.la`.
- `zf/test/Makefile.am`: build and run `ZfYAMLTest`.
- `zv/src/Makefile.am`: install `ZvYAML.hh` and compile `ZvYAML.cc` into
  `libZv.la`.
- `zv/test/Makefile.am`: build and run `ZvYAMLTest`.

Do not add an external YAML library or configure-time dependency. Do not touch
unrelated Cf users or migrate existing configuration files as part of this
change.

## `ZfYAML.hh`

### Facet and field properties

- Declare `ZuStructFacet(YAML)`.
- Add `namespace ZfYAML { using namespace ZfJSON; }`, matching `ZfCf`, so bytes,
  number, and time format vocabulary remains available.
- Add `ZuFieldProp::YAML` exactly as `ZuFieldProp::Cf` does: import the shared
  `ZuFieldProp::JSON` vocabulary so callers can spell `YAML::ID`, `YAML::Opt`,
  byte encodings, number formats, and time formats without duplicating those
  property types. Per-format customization comes from `ZuFields<O, Facet>`:
  `ZfStructRender(O, YAML, ...)` and `ZfStructRender(O, JSON, ...)` produce
  distinct field subclasses and property lists even though they reuse the same
  property vocabulary. Matcher and load/save paths must consume the selected
  YAML facet's `Field::Props`. Only YAML-exclusive behavior such as
  `ScalarFmt` needs a new property type in `ZuFieldProp::YAML`.
- Declare `ZfYAML_Fmt(...)` and `ZfYAML_StringFmt(...)` ADL customization
  points, with `AsObject`, `AsArray`, `AsString`, and default-selection behavior
  matching `ZfCf`.

### Shared node tree

Include `ZfTree.hh` and explicitly re-export the same canonical vocabulary as
`ZfCf`:

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

`ZfYAML::AnyNode`, `ZfCf::AnyNode`, and `ZfTree::AnyNode` must therefore be the
identical type. Do not derive, wrap, copy, specialize, or add a YAML node type.
Use the existing `Array`, `Object`, owned `String`, mutable `parent`, one-byte
`scalarType`, builtin sizing, unique `ZuPtr` ownership, and common
`"ZfTree.Node"` heap unchanged.

Collection nodes already initialize `scalarType` to `ScalarTC::None`, and
scalar nodes initially default to `ScalarTC::String`. After scanning a complete
plain scalar, the YAML parser overwrites that field with `Number`, `False`,
`True`, or `Null` as appropriate. `False` and `True` avoid reparsing YAML 1.1
boolean spellings. Do not add scalar subclasses or parsed numeric payloads.
Preserve `scalarType` through alias clones, replacements, `$ref` resolution,
and flattening.

Every move, replacement, clone, and merge must immediately set the existing
mutable child `parent`; a returned root must have `parent == nullptr`.

- Preserve the common physical node layout and object field order for
  deterministic diagnostics/emission even though YAML mapping order is not
  semantic.
- Detect duplicate keys by scanning the current object's field array before
  append. This is intentionally quadratic within one mapping to avoid a second
  allocation/index on the ordinary small-object path; do not replace the object
  array with a hash table or alter handler lookup layout.

### Scan state and API

Keep the public form parallel:

- `SyntaxError` retains offset, line, column, offending byte, and failed state,
  and adds a one-byte `code` member using
  `SyntaxErrorCode::{Syntax, Indent, DuplicateKey, Directive, Tag, ComplexKey,
  MergeKey, Anchor, AliasMissing, AliasActive, AliasMax, AliasDepthMax,
  NodeMax, Unicode, Escape, SecondDocument, TrailingInput}`. `fail()` records
  only the first code and location. `badSyntax()` includes a stable short reason
  string for non-generic codes while retaining the `ZfCf` message shape.
- `Scan` owns the immutable input span and parser position state.
- `Scan::scan()` and free `ZfYAML::scan()` return
  `ZuTuple<int, ZuPtr<AnyNode>>` and throw `ZfYAMLError::badSyntax` on syntax or
  trailing-input failure. This intentionally differs from Cf's const result so
  callers can move a loaded root directly through `resolve()` and `flatten()`;
  callers that do not transform it can immediately retain it as
  `ZuPtr<const AnyNode>`.
- Omit `Defines`, `PctFn`, and related accessors because they are Cf extensions,
  not generic parser structure.

Add only the state YAML needs: current absolute offset/line, indentation level,
flow/block context, and block-scalar chomping/indentation metadata. Define
file-local integer enums for scalar style, collection context, and chomping;
use `switch` for dispatch on each. Pass a small context value containing the
parent indentation and block/flow mode down `eov()`; do not build a token-vector
AST or run a separate lexer pass.

Add this public value type, using enum constants rather than anonymous numeric
literals in parser branches:

```cpp
struct Limits {
  enum {
    AliasMax = 100,
    AliasDepthMax = 32,
    NodeMax = 1<<18
  };

  unsigned aliases = AliasMax;
  unsigned aliasDepth = AliasDepthMax;
  unsigned nodes = NodeMax;
};
```

`100` follows the common conservative alias-count default; depth 32 stops
chained anchor amplification early; `2^18` nodes caps worst-case inline node
storage at roughly 128 MiB while remaining well above normal documents under
the 1 MiB `ZvYAML` input ceiling. A zero member disallows the corresponding
operation rather than disabling its limit.

Use `Scan(ZuCSpan, Limits = {})`, free
`scan(ZuCSpan, Limits = {})`, and `ZvYAML::load(const Zi::Path &, Limits = {})`.
Store a copy of `Limits` and monotonically increasing alias/node counters in
`Scan`. Count every initially parsed node and every cloned alias node; reject
before allocation when `nodes` would be exceeded. Route every parser allocation
through a private `node<Data>(parent, ...)` helper that checks/increments the
counter and then calls `newNode<Data>`; transform allocations continue to call
`newNode` directly. Store anchor records in a YAML-heap-ID `ZtArray`; each record
contains a non-owning node pointer, completion state, and maximum expansion
depth. Store owned anchor name to record-index mappings in a YAML-heap-ID
`ZmLHashKV`. Reusing a name appends a new record and replaces the mapped index;
completion updates that record by index and never restores the older mapping.
This preserves "most recent preceding anchor" even for nested name reuse and
avoids pointers into a relocating record array. Do not use the document object
array as an index. Discard both containers when `scan()` returns.

### Errors and handlers

- Clone `ZfCfError` as `ZfYAMLError`, including `fullKey`, required/type/value,
  range, enum, boolean, and syntax diagnostics, and define `ZfYAML_EXCEPT`.
- Use the `SyntaxError` code and first-failure position for malformed anchor
  names, missing/forward aliases, active/cyclic targets, and each
  expansion-budget failure; do not introduce a second parser exception path.
- Include `ZfTreeLoad.hh` and define a `YAMLPolicy` parallel to the current
  `ZfCf::CfPolicy`. It supplies YAML `GetIDs`, `GetBytesFmt`, `GetNumberFmt`,
  and `GetTimeFmt` selectors; nested YAML handler selection; scalar admission;
  boolean extraction; and `[[noreturn]]` required/type/value/boolean/range/enum
  exception functions. Its error functions alone construct `ZfYAML_EXCEPT`;
  do not add YAML conditionals or exception macros to `ZfTreeLoad`.
- `YAMLPolicy::scalar(type, mask)` admits a node exactly when its
  `ScalarTC` kind is selected by the corresponding `ZfTreeLoad::ScalarMask`
  `ZtFlags` value: `String()`, `Number()`, either `False`/`True` for `Bool()`,
  or `Null()`.
  `YAMLPolicy::boolean<T>()` returns `T{false}` or `T{true}` directly from
  `False`/`True`; every other kind dispatches `badType(node, "boolean")`.
- Keep `AsDeflt`, `As`, `AsObject`, `AsArray`, `AsStringDeflt`, `AsString`,
  `AsJSON`, `handler_`, `handler`, `save`, `saveUpd`, and `saveDel` as the YAML
  facade.
  The object and array handlers publicly derive from the corresponding
  `ZfTreeLoad::Object<YAMLPolicy, ...>` and
  `ZfTreeLoad::Array<YAMLPolicy, ...>` bases. The string handler resolves
  `ZfYAML_StringFmt` and derives from
  `ZfTreeLoad::String<YAMLPolicy, ...>`. `AsJSON` is the explicit opt-in escape
  hatch, selected with `ZfYAML_Fmt(T *)` exactly as `ZfURI::AsJSON` is selected;
  only that handler delegates whole-object emission to `ZfJSON`.
- If public parity requires `ZfYAML::loadValue`, make it the same one-line
  forwarding template as `ZfCf::loadValue`, calling
  `ZfTreeLoad::loadValue<YAMLPolicy, ...>`. Do not copy `LoadVec`,
  `loadValue_`, object field matching, required checks, conversion branches,
  vector loading, or update algorithms into `ZfYAML`.
- Retain full-span validation for numbers, enums, flags, decimal/fixed/float,
  time, and encoded bytes. YAML syntax decoding must not weaken field-level
  validation.
- The shared loader already retains lazy vector loading and direct owned scalar
  spans; preserve that path and do not create temporary contiguous copies.
- Canonical output is deterministic block YAML: mappings use one key per line,
  emitting mainstream-safe, non-ambiguous keys as plain scalars and quoting all
  other keys; nested mappings and sequences use two-space indentation, and
  sequences use `-` entries. Stream collections directly without assembling an
  intermediate tree or string. Reuse `ZfJSON` only for individual scalar
  quoting and `ZuFieldProp`-controlled scalar formatting, all of which are valid
  YAML syntax. Do not delegate default object or array emission to `ZfJSON` or
  `ZfCf`.
- Add YAML-only `ZuFieldProp::YAML::ScalarFmt<I>` with the enum values and
  shorthand properties `Plain` (the default), `SingleQuoted`, `DoubleQuoted`,
  `BlockLiteral`, and `BlockFolded`. Apply these styles to scalar fields and
  elements. `Plain` emits a string as a plain scalar only when
  doing so preserves its string type and is unambiguous to mainstream YAML
  loaders, otherwise it uses double quotes. Single quotes double embedded
  apostrophes and fall back to double quotes for controls that YAML cannot
  represent in that style. Block literal (`|`) preserves line breaks; block
  folded (`>`) deliberately folds ordinary line breaks into spaces. Select
  `-`, clip, or `+` chomping from the value's trailing newlines so literal
  output retains them exactly. For non-string scalars, first produce the same
  lexical value selected by `NumberFmt`, `TimeFmt`, or `BytesFmt`, then apply
  `ScalarFmt`; styled fields accept that string-form spelling on reload.
  Preserve null sentinels as unquoted YAML `null`. Callers should use a quoted
  or block style for non-string values only deliberately: to a schema-oblivious
  YAML/JSON consumer those values are strings, even though the typed Z loader
  reconstructs the field type from metadata.

## OpenAPI schema transforms

Export these non-template functions from `ZfYAML.hh` and implement them in
`ZfYAML.cc`:

```cpp
ZfExtern ZuPtr<AnyNode> resolve(ZuPtr<AnyNode> &&root);
ZfExtern ZuPtr<AnyNode> flatten(
  ZuPtr<AnyNode> &&root, bool oneOf = false, bool anyOf = false);
```

Both consume ownership of a mutable tree, transform it in place, and return
ownership so calls compose without casts, aliasing ambiguity, or an unowned
result:

```cpp
auto loaded = ZvYAML::load(path);
auto root = ZfYAML::flatten(ZfYAML::resolve(ZuMv(loaded.p<1>())));
```

They reproduce the observable output of:

- `/home/count0/src/kaldron-demo/factory_sim/lib/deref_schema.js`
- `/home/count0/src/kaldron-demo/factory_sim/lib/flatten_schema.js`

The JavaScript implementations are the behavioral oracle. Capture their output
as C++ golden tests before implementing the transforms; do not merely infer
OpenAPI-standard behavior from the function names.

Accept every root node kind, including a `ScalarTC::Null` root. A null
`ZuPtr` is returned unchanged by both functions; it denotes no tree and is not
the same as a YAML null node. Because ownership is consumed and returned,
`flatten()` may replace the root itself, including the reference
implementation's YAML-null-to-empty-object behavior. Neither the root address
nor descendant addresses are stable across a transform, so callers must not
retain raw pointers. Keep raw-pointer traversal/mutation routines file-local
beneath this owning API.

### Tree mutation utilities

Add these file-local utilities with succinct names:

- `fld(object, key)` returns the field index or `-1` by linear scan;
- `adopt(node, parent)` repairs the supplied root and every descendant parent;
- `clone(node, parent)` deep-clones all three data kinds and scalar metadata;
- `erase(object, index)` moves out/removes a field without a stale `ZuPtr`;
- `merge(dst, src, path)` implements the `allOf()` rules below by moving from
  `src` whenever the field is not already present;
- `same(a, b)` implements JavaScript scalar `Set` equality for array union.

Keep the existing unique-owned tree: do not add shared ownership, a second DOM,
or serialize/reparse subtrees. A resolved target is deep-cloned into the
reference site because JavaScript can share child objects but `AnyNode` has one
parent. The serialized/observable result must remain identical.

### `resolve()`

Port `deref_schema.js` literally at the JSON-shaped tree level, but expose the
C++ operation as `resolve`; in Z, `deref` conventionally means decrementing a
reference count and would be misleading here:

- Traverse iteratively with a `ZtArray` stack, avoiding recursion depth limits.
  Preserve the JavaScript work order: array children are pushed from last to
  first, while object fields are pushed in field order onto the LIFO stack.
- Act only on object nodes having a string `$ref` field.
- Resolve only local references beginning `#/`. Leave external references,
  fragment-only `#`, missing targets, scalar targets, and array targets
  unchanged.
- Resolve the remainder as an RFC 6901-style JSON Pointer from `root`: split on
  `/`, percent-decode each segment as `decodeURIComponent` does, then replace
  `~1` with `/` and `~0` with `~`. Permit numeric segments to index arrays and
  string segments to select object fields. For arrays, accept exactly the
  JavaScript array-index property spellings: `0` or a non-zero decimal integer
  without leading zeroes and within the array bounds; other spellings are
  unresolved, not numeric aliases. Percent decoding must reject malformed `%`
  triplets, invalid UTF-8, and encoded surrogate values as `decodeURIComponent`
  does.
- For a resolved object target, remove `$ref`, replace the reference object with
  a deep clone of the target's fields, and then overlay the reference object's
  sibling fields. Siblings win on collision. Preserve JavaScript property
  ordering: target field positions remain stable when overridden, and new
  sibling fields append in their original order.
- Continue traversal through the resulting children, matching the reference
  implementation's one-pass behavior. In particular, do not repeatedly resolve
  a new top-level `$ref` copied into the same node unless the JavaScript traversal
  would revisit that node.
- Implement traversal as an explicit LIFO work stack with enter/leave items.
  Push array children last-to-first. Push object children in field order, which
  intentionally visits them in reverse field order. An expansion enter item
  records the resolved source target in a file-local active-target set; its
  leave item removes it after all cloned descendants have been visited.
- If the resolved target is the reference node, one of its structural
  ancestors, or already in the active-target set, retain that `$ref` unchanged
  and continue. If the target is a descendant that will be destroyed when the
  reference object is cleared, finish cloning it before clearing the object and
  do not retain its raw pointer afterward. These rules prevent dangling source
  pointers and infinite reference graphs while preserving ordinary acyclic
  component references.

### `flatten()`

Port the control flow of `flatten_schema.js`, including its quirks; do not
implement the behavior suggested by its comment. Use a worker with the exact
logical signature `(node, oneOf, anyOf)`. The two booleans control processing of
the object passed to that invocation only.

The worker must perform these steps in this order:

1. Return `String`, `Number`, and `Bool` scalar nodes unchanged. Replace a
   `Null` node with an empty `Object`, matching `Object.assign({}, null)` in the
   JavaScript.
2. For an array, flatten every element with `(false, false)`, not with the
   worker's incoming flags, and return the rebuilt array. This is the literal
   behavior of `o.map((v) => flatten(v))`; it means a call such as
   `flatten(array, true, false)` discards `true` before visiting the elements.
3. For an object, preserve an iteration snapshot of its original fields and
   process that snapshot in order. Mutations and `allOf` merges affect the
   result object but do not add fields to or remove fields from the snapshot
   still being iterated. This matches `o = Object.assign({}, o)` followed by
   `Object.entries(o)`.
4. On an `allOf` array, delete the key, flatten every element with
   `(false, false)`, and merge each result into the current result in array
   order using the `allOf()` rules below.
5. On a `oneOf` array with `oneOf == false`, retain the key and invoke the
   worker on that array with `(true, false)`. Step 2 immediately resets the
   flags for its elements. With `oneOf == true`, the JavaScript deletes the key
   and calls `push()` on the containing object for each alternative, which
   throws `TypeError`; report a schema-transform error on the first element
   instead of inventing an alternatives destination. An empty alternatives
   array performs no `push`, so it simply removes the key without error.
6. Apply the symmetric rule to an `anyOf` array: call the array worker with
   `(false, true)` when `anyOf == false`; when `anyOf == true`, report the same
   unrepresentable-operation error for the first non-empty alternative, or
   remove an empty key without error. The `oneOf` branch always supplies
   `anyOf == false`, and the `anyOf` branch always supplies `oneOf == false`.
7. Flatten every other field with `(false, false)`.

Implement `allOf(destination, incoming)` literally:

- A `String`, `Number`, or `Bool` destination is replaced by the incoming
  value. A `Null` destination reaches JavaScript's broken object path and must
  report the transform error for any collision.
- Object/object collisions merge recursively in incoming field order. A field
  absent from the destination is moved into it. An existing field retains its
  position even when its value is replaced.
- Array/array collisions form a stable JavaScript-`Set` union: retain all
  destination elements, then append unseen incoming elements. Strings compare
  by decoded bytes, booleans by normalized truth value, nulls equal nulls, and
  numbers by parsed JavaScript-number (`double`) value rather than source
  lexeme, so `1`, `1.0`, and `1e0` are duplicates. Object and array elements
  are always distinct because the C++ tree has unique ownership; never perform
  structural equality on them.
- Object/array, array/object, `Null` collisions, or non-object incoming values
  against an object or array destination correspond to JavaScript `undefined`
  or `TypeError`. Throw the transform error at that field path; never silently
  delete the field or manufacture a null node.

The public flags remain because they are in the JavaScript API and golden tests
must cover direct calls with each true. Do not reinterpret them as propagated
"currently inside alternatives" state and do not splice nested alternatives;
the reference implementation does neither through its normal array traversal.

### Transform diagnostics and complexity

- Add `ZfYAMLError::badPointer(path, ref)`, `badMerge(path)`, and
  `badAlternative(path, key)` diagnostics for malformed percent decoding,
  incompatible `allOf` merges, and the `oneOf`/`anyOf` true-context
  object-`push` case respectively. Unresolved or recursion-guarded `$ref`
  values remain data and do not throw.
- Use explicit work stacks and move owned subtrees whenever possible. Deep copy
  only at `$ref` expansion sites where unique ownership requires it.
- Resolve each pointer in one pass over its segments. Do not add a `$ref` cache:
  `resolve()` mutates the root while traversing, so a cached non-owning target
  can become stale and can change the reference implementation's work-order
  behavior.
- Repair and test all `parent` pointers after replacement/merge so `resolve()`,
  `path()`, and load diagnostics remain correct after both transforms.

## `ZfYAML.cc` parser

Keep parsing single-pass and allocation-conscious. Scan directly from
`ZuCSpan`, exact-copy plain scalars when no decoding/folding is needed, and
allocate only the final owned node/string/container storage. Do not build an
intermediate token list.

Use the following succinct member/file-local vocabulary. Do not replace it with
a lexer/token/AST class hierarchy:

- `bol(span)` returns the byte count to the first non-indentation byte plus the
  indentation count; it recognizes LF, CRLF, and CR as line breaks and rejects
  a tab before content.
- `eol(span, flow)` returns the physical content extent and trailing-comment
  boundary under the current separation rules.
- `bot(span)` skips blank/comment-only lines and returns the next content line,
  indentation, and consumed extent while updating `position()` monotonically.
- `bos(span, context)` classifies plain, single-quoted, double-quoted, literal,
  folded, anchor-prefixed, or alias input.
- `boa(span)` scans exactly one `&name` or `*name`, requires separation after
  the name, and returns indicator, decoded name span/string, and extent.
- `eos(span, context, indent, out, scalarType)` scans and decodes exactly one
  scalar and returns its consumed extent. Factor the five styles into
  file-local helpers selected by a `switch`; keep the exact-copy plain path in
  `eos` itself.
- `bov(span, context, indent)` returns the prefix extent and `ValueTC` for the
  next scalar, sequence, or mapping, including block collection recognition.
- `eov(span, parent, context, indent, aliasDepth)` allocates and returns exactly
  one value as `{consumed, node}`, writes the value's greatest alias ancestry to
  the output reference, and is the only dispatcher that calls
  collection/scalar parsers.
- `eov_Array(span, parent, context, indent)` parses either flow or block
  sequence according to `context`.
- `eov_Object(span, parent, context, indent)` parses either flow or block
  mapping and owns duplicate-key rejection.
- `bod(span, context, indent)` recognizes comma/closing delimiters in flow mode
  and same-indent/dedent boundaries in block mode.
- `eod(span)` recognizes document start/end markers only at indentation zero.

Keep `position()`/`fail()` and the `{int, value}` negative-return convention
from `ZfCf`; a helper records the first failure only and callers propagate `-1`
without overwriting it. Use one small `Context` enum (`Block`, `FlowSeq`,
`FlowMap`) and a scalar-style enum. Do not create parallel block and flow parser
classes or duplicate `eov()`.

### Block and flow construction

- At a block value boundary, classify `-` followed by separation as a sequence;
  otherwise scan the current logical line for an unquoted `:` followed by
  separation/end and classify it as a mapping; otherwise parse a scalar. The
  lookahead is allocation-free and observes quotes, flow brackets, and comment
  boundaries.
- A block collection owns entries at exactly its starting indentation. Greater
  indentation belongs to the preceding value; smaller indentation returns to
  the caller. A non-blank indentation level that matches no open collection is
  an error. Tabs never contribute to indentation.
- After `key:` or `-`, parse an inline value when content remains. Otherwise
  consume trivia and parse the next value only when its indentation is greater,
  except that an indentless block sequence at the mapping's own indentation is
  accepted as that mapping value. If no value follows, create `Null`.
- After `-`, recognize `key: value` as the first field of a compact mapping and
  consume subsequent mapping fields at the mapping indentation. Do not special
  case compact mappings elsewhere.
- Flow sequences require comma-separated complete values and flow mappings
  require comma-separated string-key/value pairs. Permit one trailing comma.
  Reject unmatched delimiters, block collection indicators used as flow
  structure, and omitted flow-sequence elements.
- Reject an unquoted plain mapping key equal to `<<` as `MergeKey`; a quoted
  `"<<"` or `'<<'` is an ordinary JSON string key and is accepted. At a node
  boundary reject `!` as `Tag`, and at a mapping-entry boundary reject `?`
  followed by separation as `ComplexKey`; those indicators remain ordinary
  characters where the plain-scalar productions permit them.
- Plain, single-quoted, and double-quoted scalars may continue on following
  more-indented lines. Fold a single separating line break to one ASCII space,
  preserve paragraph breaks from blank lines as line feeds, strip indentation
  through the scalar's indentation level, and preserve additional indentation.
  Stop before the first non-blank line at the enclosing indentation.

### Scalar resolution

Resolve types only after a complete plain scalar has been decoded. Quoted and
block scalars are always `ScalarTC::String`, as are all mapping keys.

- Match `null` exactly as `Null`; an omitted value is also `Null` with an empty
  stored string.
- Match the listed YAML 1.1 boolean spellings exactly and set `True` or `False`;
  preserve the decoded spelling in the node string.
- Match JSON numbers, YAML 1.2 `[+-]?0o[0-7]+`, and the YAML 1.1 integer
  grammar: binary `0b`, leading-zero octal, decimal, hexadecimal `0x`, and
  sexagesimal forms, including the permitted sign and `_` separators. Match
  over the entire scalar and set `Number`; implement this as grammar scanning,
  not a regex call.
- Everything else is `String`. In particular, `~`, case variants of `null`,
  invalid radix digits or sexagesimal groups, `.inf`, `.nan`, and timestamps
  remain strings rather than acquiring YAML-only types.

Integer field loading and all numeric output use the same default field format
as `ZfCf`. A valid YAML integer spelling overrides that input default; emitters
continue to honor the field's configured `ZuFieldProp` format and do not
preserve an input spelling or radix.

Field loading must use the same scalar-kind acceptance matrix as `ZfJSON` while
retaining the `ZfCf` conversion/error machinery: string, bytes, and custom
string handlers require `String`; booleans require `True`/`False` and return the
encoded truth value without rescanning; integer, float, fixed, decimal, and time
handlers accept both `String` and `Number`; enums/flags continue through the
string conversion path when presented as strings. `Null` is not an implicit
default and produces a type error for every existing non-nullable `ZfStruct`
field. After the kind gate, retain all `ZfCf` full-span scans, formatting
properties, range checks, and diagnostics.

### Document scan

`Scan::scan()` must execute one fixed sequence: reject a null input span;
consume an optional byte-zero BOM and leading blank/comment lines; return an
empty `Object` if no non-trivia input exists; consume one optional indentation-
zero `---`; parse exactly one root value; consume one optional indentation-zero
`...`; then consume trailing blank/comment lines. An explicit marker with no
root value represents `Null`, whereas a genuinely blank/comment-only
configuration represents `{}` for `ZfCf` compatibility. Report a following
`---` as `SecondDocument` and any other remaining byte as `TrailingInput`.
Return the full input length on success, exactly as `ZfCf::scan()` does.

`---` and `...` are markers only when they occupy a complete indentation-zero
content line followed by separation/comment/end. The same bytes inside a scalar
or at nonzero indentation are scalar content or an error according to their
enclosing production; they never silently end the document.

### Anchor and alias composition

- Parse anchor and alias names using YAML 1.2 indicator/separation rules; do not
  treat `&` or `*` inside quoted/block scalars as syntax.
- Accept at most one anchor property before a value, either before an inline
  scalar/flow/block value or alone before the following indented block value.
  Reject a second anchor property, any tag property, trailing content after an
  alias, and anchors/aliases used as mapping keys.
- Register an anchor as soon as its node identity is allocated, mark it active
  while its contents are being parsed, and mark it complete only after the
  entire scalar/sequence/mapping node succeeds.
- Allow an anchor name to be reused. Each subsequent alias resolves to the most
  recent preceding anchor of that name.
- On an alias, reject a missing/forward or active target. Otherwise deep-clone
  the completed target beneath the alias site's parent, preserving scalar kind,
  object-field order, and every descendant `parent` pointer.
- `eov()` propagates a parser-temporary expansion depth equal to the greatest
  alias ancestry used below the returned node, and stores it in the anchor entry
  when completing an anchored node. An alias of a target at depth `d` has depth
  `d + 1`; reject it when that exceeds `Limits::aliasDepth`. Preflight the source
  subtree to obtain its node count before cloning, increment the alias count
  once, and reserve both budgets before allocating. Fail atomically when a
  configured budget would be exceeded; do not leave a partially appended
  subtree. The temporary depth is not retained in the public node.
- Discard anchor names and the lookup table after composition. No anchor/alias
  metadata or shared ownership remains in the returned tree, so canonical save
  emits ordinary duplicated YAML values.
- Do not implement `<<` here. A merge key is an application-level mapping merge,
  not an alias node, and remains invalid in this profile.

### Required lexical behavior

- Treat spaces, comments, line breaks, and indentation according to block vs
  flow context; `#` starts a comment only where YAML separation rules permit.
- Plain scalars end at YAML context-specific `:`, `,`, `[`, `]`, `{`, `}`, or
  comment boundaries; allow `:` and `#` when they are ordinary scalar content.
- Single quotes escape a quote by doubling it (`''`); backslash has no special
  meaning in this style.
- Double quotes implement YAML escapes, including `\0`, `\a`, `\b`, `\t`,
  `\n`, `\v`, `\f`, `\r`, `\e`, space, `\"`, `\/`, `\\`, `\N`, `\_`,
  `\L`, `\P`, `\xXX`, `\uXXXX`, and `\UXXXXXXXX`, with Unicode scalar-value
  validation and UTF-8 emission. Combine an immediately adjacent high/low
  `\u` surrogate pair for JSON compatibility; reject lone, reversed, or
  malformed surrogates and reject surrogate/out-of-range `\U` values.
- Implement YAML line folding for multiline flow scalars.
- Implement literal/folded block scalars, explicit indentation indicators, and
  strip/clip/keep chomping (`-`, default, `+`) without rescanning the whole
  document. Accept indentation digits `1` through `9`, permit chomping and
  indentation indicators in either order, reject `0`/duplicates, and infer an
  omitted indentation indicator from the first non-empty content line.
- Support indentation-based nested mappings/sequences, compact sequence
  mappings, empty values, and flow collections with optional trailing commas.
- Detect malformed dedents, missing separation after `-`/`:`, unterminated flow
  collections/quotes, invalid escapes/code points, duplicate keys, misplaced
  document markers/directives, invalid anchors/aliases, alias expansion limits,
  and all explicitly unsupported indicators at the earliest stable byte.

### Performance checks during implementation

- Keep an exact-copy path for the common one-line plain scalar and a direct
  append path for uncomplicated quoted scalars.
- Update line/column state monotonically as Cf does; do not repeatedly count
  from the beginning of the input.
- Use `ZtScratch`/`ZmScratch` only for genuinely temporary bounded parsing state
  with heap fallback and a named heap ID.
- Preserve uninitialized append/push behavior of Z containers and avoid
  default-initialize-then-overwrite loops.
- Do not add STL containers, regexes, streams, locale-dependent character
  classification, or a third-party parser.

## `ZvYAML.hh` and `ZvYAML.cc`

Mirror the non-directive parts of `ZvCf`:

- Define `ZvYAMLError`, `ZvYAML_EXCEPT`, and `ZvYAML::load` using
  `ZfYAML::AnyNode`; `load` returns the same mutable `ZuPtr<AnyNode>` result as
  `ZfYAML::scan` so the loaded document can be transformed without a cast. Its
  exact declaration is:

  ```cpp
  ZvExtern ZuTuple<int, ZuPtr<AnyNode>> load(
    const Zi::Path &, ZfYAML::Limits = {});
  ```
- Copy `enum { MaxFileSize = 1<<20 };` and reject `length >= MaxFileSize`,
  exactly matching `ZvCf`; keep its `ZiStat` validation, empty-file handling,
  read-only `ZiMMapFile` path, file-specific syntax-error rethrow, and returned
  root lifetime guarantees.
- Keep `save`, `saveUpd`, and `saveDel` as `ZiFileTxStream` wrappers around the
  corresponding `ZfYAML` functions, including open/write diagnostics.
- Do not copy `ZvCf::Reader`. It exists to retain recursive include mappings,
  define state, include paths, and callback/error-path state, none of which
  exists in YAML. Keep only a file-local `Mapped` value with `ZiMMapFile` and
  `ZuCSpan`, plus a file-local `map(path)` helper implementing `ZiStat`, the
  `MaxFileSize` check, empty-span backing storage, and read-only mmap. `load()`
  canonicalizes the supplied path for diagnostics, calls `map()`, constructs
  `ZfYAML::Scan{mapped.span, limits}`, and catches a parser exception only to
  rethrow `badSyntax` with that path and the scan's first error location/code.
  The parse tree owns every key/scalar, so it cannot retain mapped bytes after
  `load()` returns and `Mapped` is destroyed.
- Use `.yaml` in tests and examples; accepting `.yml` requires no code because
  the loader does not enforce extensions.

## Tests

### `ZfYAMLTest`

Clone the structure and reflection fixtures of `ZfCfTest`, rename the fixture
types/facet to YAML, retain the parser-integration ownership/member-path/
handler/load/update/save tests, and replace Cf grammar tests with YAML cases.
Do not copy the format-neutral layout and policy-seam matrix from `ZfTreeTest`;
keep that test as the single direct regression suite for the shared layer. Add
a compile-time assertion that `ZfYAML::AnyNode` and `ZfTree::AnyNode` are the
same type, plus YAML-specific scalar-kind integration checks.

Cover at least:

1. Owned source lifetime, parent pointers, `resolve()`, repeated handler loads,
   and immutable input.
2. Nested block mappings/sequences, flow mappings/sequences, mixtures of block
   and flow nodes, indentless mapping-value sequences, compact sequence
   mappings, omitted-value nulls, empty collections, and root
   scalar/sequence/mapping/null documents.
3. Plain-scalar delimiter/comment edge cases and all three flow scalar paths;
   exact decoded results for quote doubling, escapes, Unicode BMP/non-BMP code
   points, and invalid Unicode. Test every YAML 1.1 boolean spelling as `Bool`,
   all case variants of YAML 1.2 `true`/`false`, and the same text in quoted and
   block scalars as `String`. Pin JSON-number recognition and all supported
   YAML integer forms; verify invalid YAML numeric/null spellings remain
   `String`.
4. Multiline flow folding plus literal/folded block scalars for implicit and
   explicit indentation, both header-indicator orders, and all three chomping
   modes.
5. Blank lines, comments, CRLF/CR/LF handling, byte-zero BOM handling, blank
   input as `{}`, marker-only input as `Null`, optional `---`/`...` document
   markers, rejection of all `%` directives, and precise line/column/offset
   diagnostics.
6. Invalid indentation, indentation tabs, malformed collections, missing
   separation, unterminated scalars, invalid escapes, duplicate mapping keys,
   multiple documents, complex keys, tags, merge keys, `%TAG`, and unknown
   directives. Include acceptance of quoted `"<<"` beside rejection of the
   plain merge key.
7. Anchors on scalars, sequences, and mappings; aliases in block and flow
   contexts; duplicate anchor names resolving to the most recent definition;
   deep-copy independence; discarded anchor names; and repaired parent paths.
   Load an alias-containing tree into a reflected fixture and save that fixture
   to prove canonical output contains duplicated ordinary values rather than
   anchors. Cover malformed names, missing/forward aliases, direct/indirect
   active cycles, and each limit using deliberately small test `Limits` values.
8. The complete `ZfCfTest` field conversion matrix adapted to YAML input:
   required/default/update fields, range errors, booleans, signed/unsigned
   integers, custom integer formats, enums, flags, float/fixed/decimal,
   date/time, bytes codecs/raw bytes, vectors, nested UDTs, and string-formatted
   UDTs. Add quoted-versus-plain kind tests and verify that `Null` and the wrong
   resolved scalar kind fail before conversion.
9. `save`, `saveUpd`, and `saveDel` output parsing and semantic round trips;
   assert deterministic block YAML output, nested indentation, empty
   collections, and explicit `AsJSON` opt-in behavior.
10. `resolve()` local-pointer decoding (`%XX`, `~1`, and `~0`), object and array
   pointer traversal, sibling override/order, unresolved/external/scalar/array
   targets, chained-reference work-order behavior, recursion guarding, and
   parent-pointer repair.
11. `flatten()` recursive and ordered `allOf` object merges, stable scalar-array
    unions, scalar replacement, incompatible-kind diagnostics, recursive
    ordinary fields, and the reference implementation's `null` behavior.
12. Separate, nested, and interleaved `oneOf`/`anyOf` fixtures for all four
    `(oneOf, anyOf)` argument combinations. Pin that normal array traversal
    resets both flags and therefore retains nested combinator keys, while a
    direct true-context call on an object takes the JavaScript object-`push`
    error path. Do not write an expected fixture that splices alternatives; the
    JavaScript implementation does not do so through normal traversal.

Create the transform expected-output fixtures by running the two referenced
JavaScript modules over small JSON-shaped inputs and copying the resulting
JSON into the C++ test. The unit test must not depend on Node, npm packages, or
the external `kaldron-demo` checkout. Include one representative fixture shaped
like `factory_sim/openapi.yaml` with reusable `components/schemas`, `$ref`
siblings, nested `allOf`, `required` array unions, and `oneOf` alternatives;
assert the complete transformed tree, not only selected fields.

Use table-driven `ZuTestRepeat` cases for valid/invalid grammar, but keep
failure assertions specific enough to pin the byte and diagnostic. Include
focused examples derived from the YAML 1.2.2 productions rather than importing
the full external YAML test suite into the unit test.

### `ZvYAMLTest`

Adapt the non-include portions of `ZvCfTest`:

- mmap-backed load and lifetime after the mapping object leaves local scope;
- empty, missing, malformed, oversized, unreadable, and read-only files;
- file path plus line/column/offset in syntax diagnostics;
- nested block YAML load into reflected data;
- `flatten(resolve(ZuMv(result.p<1>())))` directly on the mutable pointer returned
  within the `ZvYAML::load` result, followed by ownership, schema-field, and
  parent-path assertions;
- file `save`/`saveUpd`/`saveDel` and reload round trips;
- write/open failures on bad destinations.

Do not copy `%include`, define expansion, include-cycle, or include-chain tests.

## Implementation sequence

1. Confirm the completed `ZfTree`/`ZfTreeLoad` precursor and its tests are green,
   then freeze the YAML scope above in tests: especially empty-document behavior,
   duplicate rejection, single-document handling, anchor/alias composition and
   budgets, and unsupported cyclic-graph/tag features.
2. Add makefile wiring and compiling header skeletons with the YAML facet,
   properties, explicit `ZfTree` re-exports, errors, `YAMLPolicy`, and thin
   `ZfTreeLoad`-derived handlers. Do not create any node or load algorithm.
3. Implement flow collections and one-line scalars first. This establishes the
   `bov`/`eov` tree construction and immediately exercises handler/load/save
   parity with Cf.
4. Add block indentation parsing for mappings and sequences, including nested
   and compact forms.
5. Add anchor registration, alias deep-copy composition, cycle detection, and
   configurable expansion budgets across block and flow nodes.
6. Add multiline quoted scalars and literal/folded block scalars with chomping.
7. Add document-marker validation, directive rejection, duplicate detection,
   unsupported-feature rejection, and exact diagnostics.
8. Capture JavaScript golden outputs, then implement the tree mutation/clone
   utilities and `resolve()` with exact traversal and overlay ordering.
9. Implement `flatten()`, including the iteration snapshot, `allOf`
   merge/JavaScript-value union behavior, array flag reset, direct true-context
   error, and null semantics.
10. Implement the `ZvYAML` mmap/file wrappers and file-level diagnostics.
11. Complete the field conversion, transform, and round-trip matrices, then
   audit the diff against `ZfCf`/`ZvCf`: every format-facade or file-layer
   divergence must correspond to a YAML rule, schema-transform requirement, or
   the absence of a Cf-only extension.

Do not refactor `ZfTree`, `ZfTreeLoad`, or `ZfCf` while introducing YAML. The
format-independent node and loader extraction is already complete; reopening
it would obscure review of the YAML-specific parser and risk the existing Cf
path. If implementation exposes a genuine common-layer defect, fix it as a
small separately reviewed precursor change with both `ZfTreeTest` and
`ZfCfTest` coverage before continuing YAML work.

## Verification and acceptance

Build lower layers before their tests, following repository build rules:

```sh
make -C zf/src -j8
make -C zf/test -j8
./zf/test/ZfYAMLTest
make -C zv/src -j8
make -C zv/test -j8
./zv/test/ZvYAMLTest
make -j8
make test
```

Before completion:

- gcc and clang builds have no warnings;
- `ZfTreeTest` remains green, there is still exactly one node representation
  and one load algorithm, and `ZfYAML::AnyNode`, `ZfCf::AnyNode`, and
  `ZfTree::AnyNode` are compile-time-identical;
- neither YAML file duplicates `ZfTree` node definitions or `ZfTreeLoad`
  object/array/vector/scalar conversion templates, and the common headers gain
  no YAML parser, error macro, transform, or save-emitter dependency;
- the existing `ZfCfTest` and `ZvCfTest` remain unchanged in behavior and pass;
- C++ `resolve()`/`flatten()` golden outputs match the checked reference
  JavaScript outputs field-for-field and in deterministic object/array order;
- anchor/alias fixtures compose to the same ordinary JSON tree as explicit
  duplicated values, and reflected save emits those ordinary duplicated values,
  while cycle and low-budget fixtures fail deterministically without retaining
  partial clones;
- malformed-input tests cover every parser branch that returns failure;
- sanitizer and valgrind runs show no leaks, invalid accesses, or ownership
  cycles;
- a code review compares the new files side by side with their Cf counterparts
  for facade/parser naming, ordering, indentation, format-specific heap IDs,
  exception style, and unnecessary divergence, while separately confirming
  both formats instantiate the common tree/load layers;
- the public comments state the supported YAML profile and rejected features,
  so callers cannot mistake the implementation for full YAML graph/tag
  conformance.
