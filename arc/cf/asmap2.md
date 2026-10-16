# `AsMap` plan for Cf, YAML, TOML, and JSON

## Goal and scope

Build on the implemented `ZfJSON::AsMap` to add the same homogeneous
associative-container mapping to `ZfCf`, `ZfYAML`, and `ZfTOML`.
`ZfJSON::AsMap` remains the behavioral and container-lifecycle reference.

In all four formats, `AsMap` is an object-level mapping and a potential
document root alongside `AsObject`. It may also be selected recursively for a
UDT field, array element, or map value.

The public tag is parallel to `AsArray`:

```c++
template <unsigned ValCode, typename ValProps = ZuTypeList<>>
struct AsMap;
```

The mapped object is always a `ZmRef` to a `ZmHash`, `ZmLHash`, or `ZmRBTree`
container. Each handler relies only on the existing container contract:

```c++
using Map = typename O::T;
using Key = typename Map::Key;
ZuAssert(ZuTraits<Key>::IsString && !ZuTraits<Key>::IsWString);
using Val = typename Map::Val;

map.add(key, val);             // load
auto i = map.citer();          // save
while (auto node = i()) {
  // node->key(), node->val()
}
```

Non-Z associative containers, heterogeneous values, non-string keys, and
run-time value dispatch are out of scope. This work requires no changes to
`ZmHash`, `ZmLHash`, or `ZmRBTree`.

`ZfCLI` and `ZfURI` are explicitly out of scope. Their flattened key grammars
do not need `AsMap` support.

## Established behavior from `ZfJSON::AsMap`

Carry the proven behavior into the three new format handlers:

1. `ctor()` and `new_()` allocate one `Map`, populate it directly, and return
   or placement-construct only the owning `ZmRef`.
2. `load()` allocates only if the destination is null. Otherwise it retains
   the current `ZmRef` and referenced `Map`, calls `clean()`, and reloads it.
3. `update()` allocates only if necessary, leaves absent keys unchanged, and
   for every supplied key calls `del(key)` followed by `add(key, value)`.
4. Values are obtained through the normal update-filtered `loadValue` path.
   An update replaces the selected map value; it does not recursively mutate
   the value already held by the container.
5. Parsed entries are consumed in parser order. The mapping does not normalize
   duplicates; parser acceptance and container uniqueness retain their native
   behavior.
6. Keep `citer()` in a narrow scope so its read lock is released immediately
   after traversal. Do not copy the map or construct an intermediate sequence.
7. Scalar and vector values use the target format's ordinary
   `saveValue*`/`loadValue*` machinery. Only `ValCode == ZfFieldTC::UDT`
   recursively resolves that format's `As<Val>` taxonomy.
8. Null, wrong-kind, validation, default, and exception behavior must match an
   ordinary `ZfStruct` member value in the target format. Do not impose JSON's
   permissive null behavior on Cf, YAML, or TOML.

Map keys are always plain strings. A map handler must consume each parsed
object member key directly and must not call `resolve`, `scanKey`, or otherwise
interpret `.`, brackets, digits, or any other characters as structure. It is
an application/user error to supply a structured key where the surrounding
format syntax does not admit one. Format-native quoting must preserve keys
which are valid as quoted object/table member names.

## 1. Add `AsMap` to the three remaining taxonomies

In `zf/src/ZfCf.hh`, `zf/src/ZfYAML.hh`, and `zf/src/ZfTOML.hh`:

1. Forward-declare `AsMap<ValCode, ValProps>` immediately after `AsArray`.
2. Define it immediately below the complete `AsArray` definition and above
   `AsString`, preserving the order `AsObject`, `AsArray`, `AsMap`,
   `AsString`.
3. Update taxonomy and dispatch comments to distinguish heterogeneous
   `AsObject` from homogeneous string-keyed `AsMap`.
4. Preserve default selection: a type with `ZfStruct` fields remains
   `AsObject`, a type without fields remains `AsString`, and maps opt in with a
   format-specific ADL declaration, for example:

   ```c++
   friend ZfYAML::AsMap<ZfFieldTC::Int32>
     ZfYAML_Fmt(MyIntMapRef *);
   ```

5. Extend only the compile-time category detectors needed by YAML/TOML local
   dispatch. Do not auto-detect map-like types or add concepts, virtual
   dispatch, or run-time mapping enums.

Confirm that `ZfJSON::AsMap` remains adjacent to and below `AsArray` and that
the JSON root save/handler APIs continue to accept both `AsObject` and
`AsMap` without a special-case restriction.

## 2. Factor common `ZfTree` map loading

Cf, YAML, and TOML all load `ZfTree::AnyNode` through `ZfTreeLoad` policies.
Add a policy-driven helper in `zf/src/ZfTreeLoad.hh`, parallel to
`ZfTreeLoad::Array`:

```c++
template <
  typename Policy, unsigned ValCode, typename ValProps,
  typename O, typename Facet>
struct Map;
```

The helper owns only format-independent loading:

- bind `Map = typename O::T`, `Key = typename Map::Key`, and
  `Val = typename Map::Val`;
- enforce the narrow string-key contract with `ZuAssert`;
- accept `AnyNode::Object` as its valid representation;
- iterate the object's immediate fields once, construct `Key` directly from
  `field.p<0>()`, and never interpret the key as a path;
- convert each value with
  `ZfTreeLoad::loadValue<Policy, Facet, Filter, ValCode, ValProps, Val>`;
- implement `ctor`, `new_`, `load`, and `update` with the lifecycle above;
- let `Policy::handler<Val, Facet>` select recursive UDT mappings and let the
  policy produce its existing typed errors for invalid values.

Do not put save logic in `ZfTreeLoad::Map`: Cf, YAML, and TOML require
different key syntax and value layout. Avoid changing the behavior of the
existing `Object`, `Array`, and `String` helpers.

## 3. Implement `ZfCf::AsMap`

Derive the handler from
`ZfTreeLoad::Map<CfPolicy, ValCode, ValProps, O, Facet>` and inherit its load
surface.

Implement a Cf-local save loop:

1. emit `null` for a null reference and `{}` for a non-null empty map,
   consistent with Cf's existing JSON-compatible emitter behavior;
2. delimit entries with commas and quote each key with `ZfJSON::quote`;
3. route scalar/vector values through the JSON scalar machinery already used
   by Cf;
4. route UDT values through `ZfCf::As<Val>::Handler`, not
   `ZfJSON::As<Val>`, so nested Cf `AsObject`, `AsArray`, `AsString`, and
   `AsMap` declarations remain effective;
5. forward `Facet`, `Filter`, `ValCode`, and `ValProps` unchanged.

Do not blindly delegate the complete map handler to `ZfJSON::AsMap`, because
that would redirect recursive UDT formatting through `ZfJSON::As<Val>`.
Factor a small Cf-local value saver if needed; keep broader changes to existing
`AsObject`/`AsArray` delegation out of scope unless correctness requires them.

Allow `ZfCf::save` and `ZfCf::handler` to operate on an `AsMap` root exactly as
they do on an `AsObject` root. Loading a scalar `null` as a map should retain
Cf's existing wrong-kind UDT error behavior.

## 4. Implement `ZfYAML::AsMap`

Derive the loader from the shared `ZfTreeLoad::Map` helper. Mark the handler as
a block, non-scalar mapping using the same local category flags as
`ZfYAML::AsObject`.

Add a YAML block-map saver parallel to the existing object field/value helpers:

1. emit `{}` at the current indentation for a non-null empty map;
2. emit YAML `null` for a null reference in the same contexts/style used for a
   null UDT value;
3. for each entry, indent, call `saveKey()` on the plain key, emit `:`, and
   route `node->val()` through the existing scalar/vector/UDT value saver using
   `ValCode` and `ValProps`;
4. preserve block indentation for nested `AsObject`, `AsArray`, and `AsMap`
   values, and preserve scalar styles for scalar and `AsString` values;
5. forward facet and save/update/delete filters through arbitrary nesting.

Refactor only enough of `saveFieldValue` and `saveArray` to share the
block/scalar layout decision. Keep YAML alias, merge, and implicit-scalar
resolution unchanged.

Allow an `AsMap` root alongside `AsObject`. A root map uses the same block-map
syntax and parsing path as a nested map; it must not be wrapped in a synthetic
field.

## 5. Add TOML map-format field properties

TOML needs an explicit choice between inline-table and table output, analogous
to its existing `InlineArray`/`TableArray` choice.

In `ZfTOML.hh`, add a map-format enum and field-property selector following the
existing array-property pattern. Use names equivalent to:

```c++
namespace ZfTOML {
  enum { InlineMap, TableMap };
}

namespace ZuFieldProp::TOML {
  template <uint8_t I> struct MapFmt { };
  using InlineMap = MapFmt<ZfTOML::InlineMap>;
  using TableMap = MapFmt<ZfTOML::TableMap>;

  template <typename Props>
  using GetMapFmt = /* InlineMap by default */;
}
```

Use final names consistent with the surrounding TOML property vocabulary and
keep them below the 32-byte name limit. Inline map is the default for a map
used as a field value, matching the existing default of inline array.

The map layout property belongs to the enclosing field's `Props`. `ValProps`
on `AsMap` continue to describe every homogeneous value, and may themselves
carry array/map formatting properties when `Val` is a nested UDT.

This is the complete dependent-facing control surface. Applications always use
the ordinary `ZfTOML::save`, `saveUpd`, or `saveDel` entry point; they do not
select a different save function for inline-map versus table-map output. The
top-level serializer reads the field property at compile time and chooses the
required representation internally.

Add compile-time tests for the default and explicit selectors, parallel to the
current `GetArrayFmt` assertions.

## 6. Implement `ZfTOML::AsMap`

Derive loading from the shared `ZfTreeLoad::Map` helper. Extend TOML's handler
classification so a homogeneous map is distinguishable from a
metadata-defined object without instantiating `ZuFields<MapRef, Facet>`.

Implement two native save paths:

### Inline map

Emit a TOML inline table:

```toml
map = { alpha = 1, "quoted.key" = 2 }
```

Add dedicated internal support for inline maps rather than routing them through
the object implementation, which requires `ZuFields`. For each entry, call
`saveKey` and the local TOML `saveValue` with `ValCode`/`ValProps`. Add an
inline-map validity check, analogous to `InlineArrayValid`, which rejects
values requiring table context. Its internal type/function names are an
implementation choice.

### Table map

When the enclosing field has the `TableMap` property, emit a table namespace
for a map field and serialize its dynamic entries within that namespace.
Recurse into UDT object, array, and map values using the same table-context
analysis as static fields. Rename the existing `saveTableArray` helper to the
concise `saveTblArray` as part of this work. If the map traversal is factored
into a corresponding named helper, call it `saveTblMap`; use `Tbl` consistently
for these internal table-context emitters.

Dynamic keys remain single plain map keys. When constructing a TOML table
path, pass each key through `saveKey` as one path component; a quoted key such
as `"a.b"` must not be split into `a` and `b`. Do not call tree `resolve` or a
generic path scanner on map keys.

Integrate map context into:

- `saveValue` UDT dispatch;
- `fieldNeedsTableContext` and `needsTableContext`;
- inline/table array validity when their elements contain maps;
- the new inline/table map validity checks;
- the second, table-emission pass of `AsObject`;
- recursive table emission for values which themselves map as `AsMap`.

The normal top-level `ZfTOML::save` dispatch must perform all of this
automatically from compile-time field metadata. Do not expose representation-
specific save entry points or require dependents to call internal traversal
helpers.

TOML has no null literal. A null map or null map value must raise the same typed
non-null error as an ordinary null TOML UDT/scalar field.

Permit `AsMap` as a TOML document root alongside `AsObject`. Relax the current
root restriction from exact `AsObject::Handler` identity to a compile-time
root-category check accepting `AsObject` or `AsMap`. A root map is inherently
in table/document context, so it emits top-level assignments and nested table
sections as needed; it is not enclosed in `{}` and does not require a field
property to select table form.

## 7. Focused tests

Keep tests in the existing format binaries:

- `zf/test/ZfStructTest.cc` for JSON regression/root coverage;
- `zf/test/ZfCfTest.cc`;
- `zf/test/ZfYAMLTest.cc`;
- `zf/test/ZfTOMLTest.cc`.

Use small format-local map types and ADL declarations. Across the matrix,
cover:

- `ZmRef<ZmHash...>`, `ZmRef<ZmLHash...>`, and `ZmRef<ZmRBTree...>`;
- compile-time extraction of `Map`, `Key`, and `Val`, including narrow-string
  enforcement;
- `ctor`, `new_`, load into null, identity-preserving load which clears old
  entries, and identity-preserving update which patches present keys and
  preserves absent keys;
- integer, boolean, string/bytes, and a property-formatted scalar;
- UDT values mapped as `AsObject`, `AsArray`, `AsString`, and nested `AsMap`;
- facet and save/update/delete filter propagation through a map of objects;
- maps nested as UDT fields and maps used directly as document roots in JSON,
  Cf, YAML, and TOML;
- empty/null maps and wrong-kind/value behavior according to each format's
  ordinary member semantics;
- plain, quoted, special-character, and UTF-8 keys, verifying that keys are
  never interpreted as lookup paths;
- parser/container-native duplicate behavior;
- iterator-order-independent assertions for `ZmHash`/`ZmLHash`.

Add format-specific assertions for:

- Cf's JSON-compatible object syntax and strict wrong-kind loading;
- YAML indentation, implicit-scalar-looking keys, scalar styles, and
  parse-save-parse equivalence;
- TOML map-property selection and validity, table-map emission, nested maps,
  quoted dotted keys as single components, null errors, and parser rejection
  of duplicate TOML keys;
- the existing Python TOML interoperability test for both inline and table map
  output.

Do not add intentionally failing non-string-key tests without a compile-fail
harness; the handler's `ZuAssert` documents and enforces that contract.

## 8. Implementation and verification order

Implement in dependency and complexity order:

1. add `ZfTreeLoad::Map`;
2. add `ZfCf::AsMap` and its tests;
3. add `ZfYAML::AsMap` and its tests;
4. add TOML map-format properties, `ZfTOML::AsMap`, root/category integration,
   inline/table output, and tests;
5. rerun the JSON map tests to guard the established reference behavior.

After each format, rebuild `zf/src`, rebuild the affected test, and run it
directly. At completion run:

```sh
make -C zf/src -j8
make -C zf/test -j8
./zf/test/ZfStructTest
./zf/test/ZfCfTest
./zf/test/ZfYAMLTest
./zf/test/ZfTOMLTest
make -C zf/test test
```

The full `zf/test` run retains coverage for unaffected CLI and URI behavior
without adding `AsMap` to either format. Run the TOML Python interoperability
check through the existing `make test` target. Then run top-level `make -j8`
and `make test` when the configured build is internally consistent. Finish
with `git diff --check` and confirm that no Z container implementation changed.

## Open questions

None. The scope excludes CLI/URI, map keys are plain non-structural strings,
all four in-scope formats permit `AsMap` roots, and TOML map layout is selected
declaratively by field properties through the ordinary top-level save APIs.
