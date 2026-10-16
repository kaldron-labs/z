# Propagate TOML save context at run time

## Goal

Refactor `ZfTOML` saving so inline versus table presentation is an explicit
run-time context passed through the complete save pipeline.  Formalize the
existing table-save state (`bool &output` and `ZuCSpan prefix`) as one small
stack-only context and fold the inline flag into it.  Pass that context through
the whole recursive save call graph; do not add a parallel `bool inline_`
parameter.

The TOML document root starts in table context.  A field carrying
`ZuFieldProp::TOML::Inline` changes its entire value subtree to inline context.
Inline context is monotonic: after it becomes true, no nested object, map,
array, pointer, or selected union alternative may emit a table header or an
array-of-tables header.

Preserve the current canonical output, parser behavior, field filtering,
optional-field handling, scalar formatting, null diagnostics, ordering, and
allocation profile.  This is an internal save-pipeline refactor, not a new
public formatting option.

## Current problem

`routeValue()` accepts `bool inline_`, but callers normally pass either the
compile-time field marker or a literal `false`.  The context is used only to
force the immediate value onto the assignment path and is then discarded:

- `saveValue()` has no context parameter and always renders composite values
  inline;
- `saveInlineObject()` and `AsMap::Handler::saveInline()` implicitly keep
  descendants inline by calling `saveValue()`;
- `saveTblObject()`, `saveTblMap()`, `saveTblValue()`, and `saveTblBody()` form
  a separate table-only call graph;
- `AsUnion` has separate inline and table entry points and redispatches with
  hard-coded context;
- `AsObject`, `AsMap`, and `AsArray` therefore do not act solely as structural
  handlers consuming one inherited presentation context.

The observable subtree rule happens to work because the call graph encodes the
context.  The implementation does not satisfy the intended invariant that the
same run-time flag is established at a field edge and forwarded unchanged
through every selected structural handler.

## Required invariants

1. The public TOML document save entry starts with `inline_ == false` and
   continues to accept only object- or map-shaped roots.  A union root remains
   valid only when its selected alternative is object/map-shaped.
2. At a field edge, derive the child context exactly once:

   ```cpp
   bool childInline = ctx.inline_ ||
     ZuTypeIn<ZuFieldProp::TOML::Inline, Props>{};
   ```

3. Put `childInline` into a child save context and pass that context to routing
   and saving.  Do not reconstruct it later from `Props`, handler type, syntax
   path, or the selected union alternative.
4. In table context:
   - object and map fields use tables;
   - non-empty arrays whose elements are all object/map-shaped use
     arrays-of-tables;
   - scalars, scalar-shaped UDTs, ordinary arrays, and mixed-shape union arrays
     use assignment values;
   - empty object/map-element arrays use `key = []`.
5. In inline context, every composite is a TOML value: objects and maps use
   `{...}`, arrays use `[...]`, and every nested composite remains inline.
6. Union dispatch forwards the already-derived context to the selected
   alternative.  The alternative's structural handler determines its route;
   the union must not replace or reset the context.
7. Entering bracket-array syntax makes each element an inline value.  Entering
   array-of-tables syntax keeps each element body in table context, where an
   element field may independently switch its own subtree to inline context.
8. Pointer unwrapping must not change context.  Preserve the existing null
   checks and errors.
9. Loading remains presentation-agnostic and requires no changes.
10. Do not introduce heap allocation, virtual dispatch, copied subtrees,
    temporary contiguous conversions, or compatibility wrappers.

## Implementation plan

### 0. Restore the common formatter structure

Use `ZfCf` and `ZfYAML` as the structural model before changing individual
TOML functions:

- `ZfCf` has one recursive `saveValue()`, one `saveField()`, and one `save()`
  implementation per `AsObject`, `AsMap`, and `AsArray` handler;
- `ZfYAML` adds run-time layout state through its internal `save_()` path, but
  still has one `saveFieldValue()`, one `saveField()`, and one `save_()` per
  structural handler; its public `save()` is only a root-context wrapper;
- both union implementations dispatch the selected value into that value's
  normal handler rather than maintaining parallel presentation-specific union
  serializers.

Make `ZfTOML` return to the same shape.  Its additional routing and two-pass
table ordering remain TOML-specific, but inline/table presentation must be a
branch inside the unified functions, not a reason to retain two recursive call
graphs.  The target internal surface is one coherent family along these lines:

```text
routeValue(..., SaveCtx)
saveValue(..., SaveCtx)
saveField(..., SaveCtx, phase/separator state)
saveObject(..., SaveCtx)
saveMap(..., SaveCtx)
saveArray(..., SaveCtx)
Handler::save_(..., SaveCtx)  // recursive structural entry
Handler::save(...)            // document-root wrapper only
```

Names may follow the closest local precedent, but there must not be parallel
`saveInline*` and `saveTbl*` recursive families after the refactor.

### 1. Introduce one internal save context

The current table path already passes `bool &output` and `ZuCSpan prefix`
together through `saveTblArray()`, `saveTblValue()`, `saveTblField()`,
`saveTblObject()`, `saveTblMap()`, `saveTblBody()`, and the corresponding union
entry points.  Factor those loose parameters into an internal context with the
equivalent shape:

```cpp
struct SaveCtx {
  bool		&output;
  ZuCSpan	prefix;
  bool		inline_;
};
```

The exact member order may follow the best local layout, but the semantics are
fixed: `output` refers to the document's existing output state, `prefix` is a
non-owning view of the current table path, and `inline_` is the inherited
presentation state.  The context owns no storage and allocates nothing.

Create the root context beside the existing local `bool output = false`, with
an empty prefix and `inline_ == false`.  Pass `SaveCtx` by reference through
all recursive save entry points, including `saveValue()`, assignment,
table-value, table-body, object, map, array, pointer, and union paths.  Create a
short-lived child context by value only when the prefix or inline state changes;
it retains the same `output` reference.  Do not retain default context values
on recursive functions and do not add a separate inline boolean parameter.

Keep `Route { Assignment, Table, TableArray }` as the centralized run-time
query.  Make `routeValue(v, ctx)` consume `ctx.inline_` directly.  When
dispatching a union, forward the context instead of substituting `false`.
An inline route always returns `Assignment`; table-context routing continues to
inspect the selected union alternative and actual empty-array state.

Audit every recursive call to ensure there is no context-free recursion and no
literal inline state below the root.  Deliberate syntax transitions construct a
child context: bracket-array elements set `inline_` true, while an
array-of-tables element body retains table context.

### 2. Derive context at object field edges

Pass the parent context into object field traversal.  For each present field,
combine it with the `TOML::Inline` marker using logical OR, then use the result
for both passes:

- the assignment pass asks `routeValue()` with the child context and, when
  selected, calls `saveValue()` with that same context;
- the table pass asks the same route with the same child context and forwards
  it to the structural table emitter.

Keep the current assignment-before-subtable ordering.  Avoid independently
deriving or querying the route in ways that could disagree for a resolved
union.  If useful, factor a small field-route helper so both passes share the
context derivation and route query without storing per-field state or
allocating.

When the parent itself is inline, emit its fields inside `{...}` but use the
same field traversal/value machinery.  Field-level `Inline` below that point is
an idempotent no-op because `true || marker` remains true.

### 3. Give each structural handler one context-aware save path

Keep `AsObject`, `AsMap`, and `AsArray` as the only structural formatters.
Replace presentation-specific handler APIs such as `saveInline()` with a
single internal `save_()` operation, following `ZfYAML`, that receives
`SaveCtx`.  Retain `save()` only as the root wrapper that constructs table
context and calls `save_()`; it must not contain another serializer.

- One `saveObject()` chooses `{...}` versus table body/header behavior from
  `ctx.inline_`.  Both branches use the same `saveField()` implementation for
  field presence, field context derivation, keys, filters, route queries, and
  recursive values.  Table context may invoke it in assignment and subtable
  phases to preserve canonical ordering; inline context invokes it once with
  comma separator state.
- One `saveMap()` chooses `{...}` versus table entries from `ctx.inline_`.
  Share one iterator-driven entry routine for key formatting, route queries,
  and recursive values; table context may perform the existing two passes,
  while inline context performs one pass.
- One `saveArray()` uses the context and element shape to choose bracket-array
  versus array-of-tables output.  Once bracket syntax is chosen, pass an inline
  child context to all elements.
- One `saveValue()` owns scalar/vector/UDT dispatch and invokes the selected
  handler's single recursive entry.  Do not preserve an inline-only
  `saveValue()` alongside a table-value dispatcher.

Small syntax-specific helpers may remain for delimiters, headers, and separator
placement only.  They must be leaf emitters: they may write punctuation or a
header, but must not traverse fields/elements, dispatch unions, call structural
handlers, or infer presentation from which helper was called.

### 4. Simplify union dispatch

Replace `AsUnion::Handler::saveInline()`, `saveTblValue()`, and
`saveTblBody()` with one `save_()` that forwards `SaveCtx` into the selected
alternative's normal structural save path, mirroring the single-dispatch shape
used by the `ZfCf` and `ZfYAML` union handlers.  Retain one route query that:

- returns `Assignment` immediately in inline context;
- in table context, dispatches to the selected alternative and determines
  `Assignment`, `Table`, or `TableArray` from that alternative;
- preserves the all-object/map compile-time test only for deciding whether an
  array with union elements is eligible for array-of-tables syntax.

Do not add common fake `Object`, `Map`, `Array`, `ElemCode`, or `ValCode`
metadata to heterogeneous unions.  Preserve unresolved-union and null-pointer
save errors.

### 5. Preserve table paths and output state

Continue building quoted table paths with `PathBuf`/`ZtScratch`.  Construct a
child `SaveCtx` whose `prefix` views the scratch path only for the synchronous
recursive call, and preserve the shared `output` reference so blank-line and
trailing-newline behavior remains unchanged.  Presentation context is
orthogonal to:

- the current table path;
- whether prior output exists;
- assignment separator state.

Keep per-container separator state such as `first` local; it is not inherited
save context.  Do not put the destination stream, owned buffers, type erasure,
or heap state into `SaveCtx`.

### 6. Remove obsolete split-path code

Perform this deletion as part of the refactor, not as optional cleanup.  After
all recursive callers use the context-aware path, delete every redundant
inline/table handler entry point and every helper whose purpose was to encode
inherited context through function selection.  In particular, replace and
remove:

- `saveInlineObject` versus `saveTblObject`;
- `AsMap::Handler::saveInline` versus `saveTblMap`;
- `AsUnion::Handler::saveInline`, `saveTblValue`, and `saveTblBody`;
- hard-coded `false` arguments in map and union routing;
- recursive `saveValue()` calls from arrays, maps, and unions.

The completed implementation must have no recursive function named
`saveInline*` or `saveTbl*`.  Header/punctuation leaf helpers should instead be
named for the syntax they emit, such as a table header, not for a competing
serialization path.  Do not leave forwarding shims for the removed internal
functions.

Compare the resulting save section side by side with `ZfYAML.hh` and
`ZfCf.hh`.  TOML should differ because it carries `SaveCtx`, performs route
selection, and preserves assignment-before-table ordering—not because it has
parallel structural serializers.

## Test plan

Extend `zf/test/ZfTOMLTest.cc` with exact-output and round-trip tests.  Reuse
existing fixture types where practical, but add focused nested types when they
make the inherited-context boundary unambiguous.

1. Save the same nested object graph from an ordinary field and an `Inline`
   field.  The ordinary form must use nested table headers; the inline form
   must contain only braces/brackets beneath the assignment.
2. Save an `Inline` object containing an unmarked object child which itself
   contains an unmarked non-empty array of objects.  Expect a single inline
   assignment such as `{child = {rows = [{...}]}}` and assert that the output
   contains neither `[` table headers nor `[[` array-of-tables headers outside
   value delimiters.
3. Exercise the same propagation through an inline map containing an object,
   and through an inline array containing objects/maps.
4. Exercise a heterogeneous union at multiple depths.  In table context its
   selected object/map alternatives must use tables and its scalar/array
   alternatives assignments; below an inline ancestor every alternative must
   remain inline.
5. Exercise an array whose element type is a union of only object/map shapes:
   table context uses array-of-tables, while an inline ancestor uses a bracket
   array of inline values.
6. Retain and explicitly cover empty object/map-element arrays in both contexts;
   both must preserve the key as `[]`.
7. Cover object and container pointers at an inline boundary, including the
   existing null failure behavior.
8. Round-trip every new canonical output through `ZfTOML::scan()` and the
   corresponding handler so the refactor cannot accidentally narrow loading.
9. Keep existing scalar-style, filtering (`save`, `saveUpd`, `saveDel`), key
   quoting, sibling table ordering, and interoperability output assertions
   unchanged.

The deepest regression should place `TOML::Inline` only on the outer field.
No descendant should require its own marker for the test to pass.

## Validation

1. Search the save pipeline for every context-bearing declaration and call:

   ```sh
   rg -n "save(Value|Assignment|Tbl|Inline)|routeValue|inline_" \
     zf/src/ZfTOML.hh zf/test/ZfTOMLTest.cc
   ```

   Confirm context starts false at the document root, changes at a field
   carrying `TOML::Inline` or upon entering bracket-array value syntax, and is
   never reset below an inline ancestor.

   Also confirm that no split recursive serializer remains:

   ```sh
   rg -n "saveInline|saveTbl" zf/src/ZfTOML.hh
   ```

   The search must return no recursive save entry point.  Any surviving match
   must be a non-recursive syntax leaf and should be renamed to describe the
   emitted syntax rather than inline/table control flow.

2. Build and run the focused TOML tests:

   ```sh
   make -C zf/src -j8
   make -C zf/test -j8
   ./zf/test/ZfTOMLTest
   ```

3. Run the format test suite and TOML interoperability check:

   ```sh
   make -C zf/test test
   make -C zf/interop test
   ```

4. Compile downstream template instantiations and check whitespace:

   ```sh
   make -j8
   git diff --check
   ```

## Completion criteria

- Inline/table state is an explicit run-time value in one `SaveCtx` throughout
  recursive TOML saving rather than an implicit consequence of selecting a
  function family.
- `SaveCtx` folds together the existing output/prefix state and `inline_`; no
  parallel inline boolean is threaded through the call chain.
- The root begins in table context and `TOML::Inline` is the only operation
  at a field edge that changes inherited context.  Entering bracket-array
  value syntax also makes its elements inline; array-of-tables bodies remain in
  table context.
- Once inline, all descendants remain inline across objects, maps, arrays,
  pointers, and runtime-selected union alternatives.
- Structural handlers consume presentation context without exposing parallel
  inline/table save APIs or duplicating recursive serialization logic.
- The save section structurally aligns with `ZfYAML`/`ZfCf`: one value path,
  one field path, one context-aware recursive entry per structural handler, and
  a thin root `save()` wrapper.
- No recursive `saveInline*` or `saveTbl*` function family remains; TOML's
  presentation branches live inside the unified object, map, array, field, and
  value functions.
- Existing canonical output and errors remain unchanged, including empty-array
  assignments, assignment/table ordering, quoted paths, and final newlines.
- All focused, format-suite, interoperability, and downstream builds pass with
  no new allocation or compatibility layer.
