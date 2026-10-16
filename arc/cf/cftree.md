# `ZfTree` / `ZfTreeLoad` precursor implementation plan

## Objective

Factor the representation tree and format-independent load machinery out of
`ZfCf` before implementing `ZfYAML`. The result must let both parsers construct
the same concrete `AnyNode` tree and let both format namespaces reuse one
compile-time load engine, while retaining format-specific parsing, scalar
admission, errors, ADL formatting, and direct-to-stream saving.

This is a behavior-preserving `ZfCf` precursor. Do not add `ZfYAML`, change Cf
grammar, change Cf load semantics, or alter Cf output in this change. Complete
and verify this extraction before beginning the YAML plan.

The dependency split is:

```text
ZfTree.hh       owned tree, scalar metadata, paths, allocation
     |
ZfTreeLoad.hh   policy-driven object/array/scalar loading
     |
ZfCf.hh         Cf policy, Cf parser API, Cf ADL handlers, Cf saving

ZfYAML.hh       later depends on the same two common headers
```

`ZfTree` is the canonical namespace for the common node type. `ZfCf` remains
a format facade that re-exports the tree vocabulary used by its scanner and
callers; it alone owns the Cf format. Future `ZfYAML` will own YAML parsing and
format behavior while reusing the same format-neutral tree and loader. This is
intentional API layering, not a second tree or compatibility wrapper.

## Non-goals

Do not factor or generalize:

- `ZfCf::Scan`, its parser routines, `Defines`, `PctFn`, or directives;
- syntax-error state or `ZfCfError::badSyntax`;
- `ZfCf_Fmt`, `ZfCf_StringFmt`, or their eventual YAML counterparts;
- `save`, `saveUpd`, `saveDel`, or any stream emitter;
- `ZvCf`, file mapping, includes, or file diagnostics;
- JSON, Cf, and YAML parsers behind a common lexer or parser base;
- existing downstream components merely to replace the spelling
  `ZfCf::AnyNode` with `ZfTree::AnyNode`.

Saving walks the destination C++ object and writes directly to the selected
format stream. It does not consume the parse tree and therefore does not belong
in `ZfTree` or `ZfTreeLoad`. `ZfCf` continues delegating its save handlers to
`ZfJSON`; the later YAML wrapper decides its own emitter independently.

## Files and build wiring

Create:

- `zf/src/ZfTree.hh`: installed, header-only tree implementation;
- `zf/src/ZfTreeLoad.hh`: installed, header-only load templates;
- `zf/test/ZfTreeTest.cc`: tree invariants and policy-seam tests.

Modify:

- `zf/src/ZfCf.hh`: include the two common headers, define the Cf load policy,
  retain the parser/errors/ADL/save facade, and remove extracted definitions;
- `zf/src/ZfCf.cc`: use the re-exported common tree and initialize Cf scalar
  metadata without otherwise changing parsing;
- `zf/src/Makefile.am`: install both headers;
- `zf/test/Makefile.am`: build and run `ZfTreeTest`.

Do not add a `.cc` file for either common layer. `AnyNode::resolve`, `path`,
`data`, `newNode`, and all load machinery are currently inline/templates; keep
the extraction mechanical and avoid introducing a new exported ABI surface.

## `ZfTree.hh`

### Canonical tree vocabulary

Move these definitions from `ZfCf.hh` into `namespace ZfTree` without
changing their array/object/string representation:

- `AnyNode`;
- `Node_HeapID`;
- `Node_` and `Node`;
- `ValueTC`;
- `NodeArray` and `CNodeArray`;
- `newNode()`;
- `isalpha__`, `isdigit__`, and `isword__` only if both common loading/tree code
  still use them; otherwise leave parser-only character helpers in `ZfCf`;
- `AnyNode::has()`, `data()`, member-path `resolve()`, and `path()`.

Keep the three physical data alternatives exactly as they are:

```cpp
using TL = ZuTypeList<Array, Object, String>;
```

Do not introduce a variant, virtual data accessor, shared ownership, STL
container, or separate YAML node subclasses. `ValueTC::{Array,Object,String}`
must retain their existing values so the Cf parser's dispatch is unchanged.

Use common heap IDs rooted at `"ZfTree.Node"` for node, string spill, array,
and object allocations. A common concrete node type cannot retain genuinely
format-specific embedded container/string heap types. Sharing this pool is the
deliberate cost of sharing the exact tree and is preferable to adding runtime
format dispatch merely for heap telemetry.

### Scalar metadata and ancestry

Add:

```cpp
namespace ScalarTC {
  enum { None, String, Number, False, True, Null };
}
```

Add a one-byte `scalarType` member to `AnyNode`. Collection nodes initialize it
to `ScalarTC::None`; scalar nodes initialize it to `ScalarTC::String`. The Cf
scanner therefore preserves its current target-directed semantics. The later
YAML scanner will overwrite the scalar value with `Number`, `False`, `True`, or
`Null` after resolving a complete plain scalar.

Change `AnyNode *const parent` to `AnyNode *parent`. Existing Cf callers still
receive `ZuPtr<const AnyNode>` and cannot mutate through that API. Mutability is
an internal tree capability required for the later YAML transforms.

Keep member order compact: virtual/base layout first, then `parent`, `type`, and
the one-byte scalar metadata in existing padding where the compiler permits.
Retain `LNodeSize`, `SNodeSize`, builtin capacity calculations, and their
assertions. Add compile-time/runtime size checks in `ZfTreeTest` so the new
metadata does not silently reduce the intended builtin payload capacity.

### Format facades

In `ZfCf.hh`, re-export rather than derive or wrap:

```cpp
namespace ZfCf {
  using ZfTree::AnyNode;
  using ZfTree::Node_;
  using ZfTree::Node;
  using ZfTree::NodeArray;
  using ZfTree::CNodeArray;
  using ZfTree::newNode;
  namespace ValueTC = ZfTree::ValueTC;
  namespace ScalarTC = ZfTree::ScalarTC;
}
```

Use explicit declarations/namespace aliases rather than a broad
`using namespace ZfTree`. `ZfCf::AnyNode` must be the identical type, not a
base, derived class, forwarding wrapper, or duplicated alias tree. The later
`ZfYAML` facade will re-export the same declarations.

Do not mass-edit current dependents that name `ZfCf::AnyNode`; the facade is the
correct format-facing spelling and resolves to the canonical type. New code
that is explicitly format-neutral may name `ZfTree::AnyNode` directly.

## `ZfTreeLoad.hh`

### Purpose and dependency direction

Move only format-independent loading into `namespace ZfTreeLoad`. This header
depends on `ZfTree`, `ZfStruct`, codecs, number/time scanning, `ZuMatcher`,
the existing Z containers, and `ZfJSON` for the already-shared property and
scalar-scan vocabulary. It must not call a `ZfJSON` save handler. It must not
include `ZfCf.hh`, `ZfYAML.hh`, or either parser, and it must not know either
format's component name or ADL hook.

All variation is compile-time. Do not add virtual functions, `std::function`,
runtime format enums, heap-allocated callbacks, or type-erased handlers.

### Policy contract

Define the common loaders with a `Policy` template parameter. Document and use
one exact policy contract. The policy supplies:

- `GetIDs<Fields>`, `GetBytesFmt<Props>`, `GetNumberFmt<Props>`, and
  `GetTimeFmt<Props>` aliases/constants from the format's field-property
  namespace;
- scalar admission returning an owned scalar span for a requested mask;
- boolean extraction, allowing Cf to scan text and YAML later to use
  `ScalarTC::False`/`True` without rescanning;
- format-component exceptions for required, bad type/value/bool/range/enum;
- nested UDT handler selection for the format's `As<T>` result.

Use an integer scalar mask local to `ZfTreeLoad`, with bits for `String`,
`Number`, `Bool`, and `Null`. The common conversion code requests the semantic
kinds it accepts; the policy decides whether the actual node is admissible.
Collections continue to be checked through `ValueTC` and do not use the mask.

The Cf policy must preserve current behavior exactly:

- every Cf scalar is `ScalarTC::String`;
- string, boolean, numeric, enum, flags, bytes, and time conversions receive
  that text just as they do before extraction;
- booleans continue through `ZtScanBool<true>` and retain `badBool` diagnostics;
- null has no new Cf spelling or behavior.

Design the policy seam so the later YAML policy can instead admit the same
scalar combinations as `ZfJSON`: strings/bytes/custom strings accept `String`,
booleans accept `False`/`True`, numeric and time fields accept `String` or
`Number`, and `Null` fails for existing non-nullable fields. Do not implement
the YAML policy in this precursor.

Policy error functions must be `[[noreturn]]` thin functions that construct the
format's existing `Ze` exception. The common loader calls them and contains no
`ZfCf_EXCEPT` or future `ZfYAML_EXCEPT` macro. This keeps the shared templates
independent of component identity and avoids duplicated conversion branches.

### Common loader templates

Extract and rename the load-only portions into these templates:

```cpp
template <typename Policy, typename O, typename Facet>
struct Object;

template <typename Policy, unsigned ElemCode, typename ElemProps,
  typename O, typename Facet>
struct Array;

template <typename Policy, typename StringHandler, typename O>
struct String;

template <typename Policy, typename Facet,
  template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
struct LoadVec;

template <typename Policy, typename Facet,
  template <typename> class Filter,
  unsigned TypeCode, typename Props, typename T>
auto loadValue(const ZfTree::AnyNode *);
```

Use `ZuDerive` aliases where the instantiated common template type would
otherwise materially lengthen symbols; retain this conceptual surface and do
not expose parser types.

`Object` owns the existing field matcher, lookup array, required checks,
constructor/init/load/update dispatch, and default/vector handling. It obtains
field IDs and exceptions solely through `Policy`.

`Array` owns existing constructor/load/update behavior and uses the policy's
nested handler for UDT element updates. `LoadVec` retains lazy conversion over
`CNodeArray`; do not materialize a temporary output array.

`String` owns only the node/span and ctor/new/load/update mechanics. The
format-specific wrapper resolves `ZfCf_StringFmt` (or later
`ZfYAML_StringFmt`) and supplies its handler type. It does not move the ADL
customization point into the common namespace.

`loadValue()` owns all existing bytes, integer, enum, flags, floating,
fixed/decimal, time, vector, and UDT branches. Replace direct
`ZuFieldProp::Cf` references with `Policy` selectors and direct exception macros
with policy calls. Preserve full-span validation, range checks, boxed values,
codec allocation, and return types byte-for-byte in behavior.

Avoid a policy/include cycle for nested UDTs: forward-declare the
format-specific `handler_` before defining its policy, and define the policy's
dependent handler function only after the format's `As` alias is available.
Template instantiation must see the completed definition without making
`ZfTreeLoad.hh` include the format header.

## Thin `ZfCf` handlers and saving

Keep `ZfCf::AsDeflt`, `As`, `AsObject`, `AsArray`, `AsStringDeflt`, `AsString`,
`handler_`, `handler`, `save`, `saveUpd`, and `saveDel` as the public
format-specific API.

Their nested handlers become thin compositions:

- `AsObject::Handler` publicly derives from
  `ZfTreeLoad::Object<CfPolicy, O, Facet>`, defines a concise `Base` alias,
  inherits its constructor/load methods, and retains a static `save()` that directly invokes
  `ZfJSON::AsObject::Handler`;
- `AsArray::Handler` publicly derives from the common array loader and retains
  its direct `ZfJSON::AsArray` save call;
- `AsString::Handler` resolves `ZfCf_StringFmt`, publicly derives from the
  common string loader instantiated with that formatter's handler, and invokes
  the same formatter directly for save;
- `handler_` retains Cf's public type validation and exception identity while
  returning the thin combined handler.

Do not put a dummy save operation in `ZfTreeLoad`, and do not parameterize the
common loader on an emitter. Loading and saving share the public handler facade
but not their implementation path.

The common bases are statically dispatched and have no virtual functions. If a
base carries `node`/`lookup`, do not duplicate those members in the wrapper.
There must be no additional allocation, virtual dispatch, forwarding layer, or
per-handler format state.

## Error ownership

Keep syntax and directive diagnostics in `ZfCfError`. Keep node-path load
message builders (`fullKey`, required, bad type/value/bool/range/enum) there as
well; the Cf policy calls them.

Do not move these builders into `ZfTreeLoad` merely to reduce a few lines: their
message component and public namespace are format-facing. The later YAML policy
will use its corresponding builders while still sharing the loader algorithm.

Exception text, component (`"ZfCf"`), node paths, range formatting, backtrace
capture, and the first failing field must remain unchanged in the Cf tests.

## Tests

### `ZfTreeTest`

Construct nodes directly through the common API and verify:

1. `ZfCf::AnyNode` and `ZfTree::AnyNode` are the same type at compile time.
2. Array/object/string data access and `ValueTC` values retain the current
   layout and behavior.
3. Collections use `ScalarTC::None`; Cf-style scalars default to
   `ScalarTC::String`; all six scalar codes survive manual construction and
   reparenting in the test.
4. Mutable reparenting produces correct `path()` output for object and array
   ancestry, including a moved subtree and a null-parent root.
5. Member-path `resolve()` retains all existing valid/invalid index and field
   behavior, including integer overflow rejection.
6. Owned strings survive destruction/mutation of the source span and spill
   beyond builtin capacity through the named common heap.
7. `ZuPtr<AnyNode>` converts to `ZuPtr<const AnyNode>` with the same destruction
   and ownership behavior.
8. A test policy exercises scalar-mask acceptance independently of the Cf
   parser: String, Number, both booleans, Null rejection, and bad-type dispatch.

Do not reproduce the full reflected field matrix in this new test; it remains
in `ZfCfTest` and is the regression oracle for the extracted load engine.

### Existing tests

Run the complete existing `ZfCfTest` without weakening assertions. Add focused
assertions that a Cf parse marks all scalar nodes `ScalarTC::String`, including
text later loaded as bool, integer, float, enum, flags, or time.

Run `ZvCfTest` and the full build because many dependent modules name
`ZfCf::AnyNode` in public signatures. No dependent source edit should be needed;
the namespace facade must preserve those declarations as the identical common
type.

Capture representative exception strings and serialized output before the
extraction and assert exact equality afterward. At minimum cover required,
bad type, bad boolean, enum, range, encoded bytes, number, time, `save`,
`saveUpd`, and `saveDel`.

## Implementation sequence

1. Add `ZfTree.hh` by mechanically moving the current tree with no semantic
   change; add explicit `ZfCf` re-exports and make `ZfCfTest` compile and pass.
2. Add mutable `parent`, `ScalarTC`, and common heap IDs. Set Cf scalar nodes to
   `String`, collections to `None`, and add `ZfTreeTest` invariants.
3. Add `ZfTreeLoad.hh` and the Cf policy. Move `loadValue`, `LoadVec`, and the
   load-only object/array/string handler bodies one unit at a time, running the
   field matrix after each move.
4. Reduce the public Cf handlers to thin load-plus-save compositions. Verify
   that saving still calls the same direct formatter and that no common loader
   template mentions saving.
5. Remove the old node/loader definitions only after all callers use the common
   implementation; do not leave duplicate implementations or forwarding
   overloads.
6. Audit includes and move codec/number/time dependencies to `ZfTreeLoad.hh` when
   they are no longer directly required by `ZfCf.hh`. Keep direct dependencies
   explicit rather than relying on transitive includes.
7. Build all dependent modules and compare symbols, node sizes, heap IDs,
   diagnostics, and output. Begin `ZfYAML` only after this precursor is green.

Do not combine this extraction with unrelated `ZfCf` cleanup. Mechanical
renaming, the scalar/parent additions required by the common model, and the
policy seam are the only permitted structural changes.

## Verification and acceptance

Run:

```sh
make -C zf/src -j8
make -C zf/test -j8
./zf/test/ZfTreeTest
./zf/test/ZfCfTest
make -C zv/src -j8
make -C zv/test -j8
./zv/test/ZvCfTest
make -j8
make test
```

The precursor is complete only when:

- there is exactly one `AnyNode`/`Node`/`Array`/`Object`/`String`
  implementation;
- there is exactly one object/array/vector/scalar load algorithm;
- `ZfCf::AnyNode` is the exact canonical common type;
- Cf parser results, reflected loads, exceptions, and serialized bytes are
  unchanged except for the intentionally new scalar metadata and common heap
  telemetry names;
- common headers do not include or name Cf/YAML parser state or save emitters;
- Cf handlers add no runtime indirection, allocation, tree conversion, or
  temporary contiguous copy;
- existing downstream code compiles without a second node type or pointer cast;
- gcc and clang builds are warning-free, and sanitizer/valgrind runs show no
  ownership, reparenting, or lifetime regressions;
- `yaml.md` can subsequently replace its copied-tree/copied-loader steps with
  direct dependence on this completed common layer.
