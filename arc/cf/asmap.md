# `ZfJSON::AsMap` implementation plan

## Goal and scope

Add `ZfJSON::AsMap` as a fourth object-level mapping alongside `AsObject`,
`AsArray`, and `AsString`. An opted-in map is serialized as a JSON object, but
unlike `AsObject` its members are not described by heterogeneous `ZfStruct`
fields: every member key has one compile-time type and every member value has
one compile-time type.

The supported object type `O` is always a reference to a Z associative
container:

- `ZmRef<ZmHash<...>>`;
- `ZmRef<ZmLHash<...>>`;
- `ZmRef<ZmRBTree<...>>`.

The mapping depends only on the referenced container's existing contract:

```c++
using Map = typename O::T;
using Key = typename Map::Key;
ZuAssert(ZuTraits<Key>::IsString && !ZuTraits<Key>::IsWString);
using Val = typename Map::Val;
```

`map.add(key, val)` loads an entry. Saving uses:

```c++
auto i = map.citer();
while (auto node = i()) {
  // save node->key() and node->val()
}
```

If `Key` exists and is a string, `Val` is compatible with the selected value
type code/properties, and the required `add`/`citer` operations exist, `AsMap`
has the information it needs.

Non-Z associative containers, including STL containers and adapters for them,
are out of scope. Do not add generic range, pair, or `std::map` detection.
This work requires no changes to `ZmHash`, `ZmLHash`, or `ZmRBTree`.

`AsMap` carries a homogeneous value type code and property list, just as
`AsArray` carries a homogeneous element type code and property list. Scalar
values use the existing `saveValue*`/`loadValue*` machinery. When the value code
is `ZfFieldTC::UDT`, that machinery recursively resolves `As<Val>`, which may
select `AsObject`, `AsArray`, `AsString`, `AsMap`, or a custom compatible
formatter. `AsArray` continues to infer its element C++ type with:

```c++
using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
```

This work does not add heterogeneous per-entry dispatch, a map analogue of
`ZfStruct` field metadata, or support for non-string JSON object keys.

## 1. Add the mapping tag and dispatch integration

In `zf/src/ZfJSON.hh`, declare `AsMap` with the other mapping tags, using an API
parallel to `AsArray`:

```c++
template <unsigned ValCode, typename ValProps = ZuTypeList<>>
struct AsMap;
```

Update the nearby taxonomy comments to distinguish:

- `AsObject`: heterogeneous, metadata-defined JSON object;
- `AsArray`: homogeneous JSON array;
- `AsMap`: homogeneous string-keyed JSON object;
- `AsString`: scalar represented as a JSON string.

Place the complete `AsMap` definition immediately after the complete `AsArray`
definition and before the `AsString` implementation. The source order should
therefore be `AsObject`, `AsArray`, `AsMap`, `AsString`; `AsMap` must be adjacent
to and below `AsArray`.

No default-selection rule should change. Types continue to opt in with ADL, for
example:

```c++
friend ZfJSON::AsMap<ZfFieldTC::Int32> ZfJSON_Fmt(MyIntMapRef *);
friend ZfJSON::AsMap<ZfFieldTC::UDT> ZfJSON_Fmt(MyObjectMapRef *);
```

`AsDeflt_` should still choose `AsObject` for types with fields and `AsString`
otherwise. Do not auto-detect map-like types or change the behavior of existing
Z container references; explicit `AsMap` opt-in keeps the mapping decision with
the dependent.

## 2. Implement `AsMap::Handler`

Add a handler with the same surface used by the other object-level mappings:
`valid`, constructor from `AnyNode`, `save`, `ctor`, `new_`, `load`, and
`update`.

At handler instantiation:

1. Bind `O` to the `ZmRef` type and `Map = typename O::T` to its referenced
   container.
2. Bind `Key = typename Map::Key` and enforce
   `ZuAssert(ZuTraits<Key>::IsString && !ZuTraits<Key>::IsWString)`, matching
   the narrow string types accepted by existing `ZfStruct` JSON member-value
   loading.
3. Bind `Val = typename Map::Val`; use the enclosing `ValCode` and `ValProps`
   template arguments for every value.
4. Treat `AnyNode::Object` as the only valid parsed representation.

Saving should:

1. Emit `{` and obtain a scoped `o->citer()` from the referenced map.
2. For every returned node, emit the comma delimiter, JSON-quote
   `node->key()`, emit `:`, and call
   `saveValue<Facet, Filter, ValCode, ValProps>` on `node->val()`.
3. Emit `}` and let the iterator leave scope immediately after traversal so any
   container read lock is released promptly.

The existing `Filter` template parameter must be forwarded unchanged to nested
values. This is important when a map value is `AsObject`, because save/update/
delete field filters must continue through every map nesting level.

Loading should iterate `node->data<AnyNode::Object>()` once and, for each parsed
field, obtain the homogeneous `Val` with
`loadValue<Facet, ZfFieldFilter::Load, ValCode, ValProps, Val>` and call
`map.add(Key(field.p<0>()), val)`. Add every parsed occurrence in source order;
do not normalize duplicate keys. Uniqueness and duplicate-key behavior belong
to the selected container.

Keep traversal and value conversion in shared private helpers used by `ctor`,
`new_`, `load`, and `update`. Use `ZfFieldFilter::Load` for construction/load and
`ZfFieldFilter::Upd` for update. This reuses the existing scalar, vector,
null/default, validation, and UDT behavior rather than duplicate it inside
`AsMap`. If a value has the wrong JSON node kind, add the null/default result
returned by `loadValue`, matching ordinary `ZfStruct` member-value loading.

Follow existing `ZfStruct` member-value behavior for JSON nulls and wrong node
kinds: a map value resolves to a null `ZmRef`. A valid JSON object resolves to a
non-null map reference, including when the destination reference was initially
null. Saving a null map reference emits `null`; saving a non-null empty map emits
`{}`.

## 3. Construct and load maps through `ZmRef`

The underlying containers do not need to be copied or moved. `ctor()` should
allocate the referenced `Map`, retain it in `O`, populate it through `add`, and
return the movable `ZmRef`. `new_` should placement-construct the same reference
result. This uses the existing handler API without adding constructors, aliases,
loader protocols, or any other changes to the three Z container classes.

Factor allocation/population so all entry points follow one conversion path:

1. Validate that the node is an `AnyNode::Object`.
2. Construct an `O` owning a new `Map` for `ctor()`/`new_`. For `load`/`update`,
   allocate only if the destination reference is null; otherwise retain and use
   the current `ZmRef` and referenced `Map`.
3. For `load`, call the container's existing `clean()` before adding the parsed
   entries. The `ZmRef` itself must not be replaced.
4. For `update`, do not clean the map: patch only keys present in the JSON
   object and leave absent keys unchanged. Remove the container-selected entry
   for each supplied key, then add the value produced by `loadValue` with the
   update filter. Leave uniqueness semantics to the container.
5. Convert each parsed value through `loadValue*` and add or update it using the
   selected load/update filter.
6. Return or store only the `ZmRef`; never materialize a temporary container.

The distinction is intentional: `load` replaces the map contents in place by
cleaning and reloading the current referenced container, while `update` patches
the current contents in place. Both preserve the identity of an existing
`ZmRef` and `Map`.

## 4. Preserve scalar formatting and UDT recursion

Route every map value through the usual field-value entry points:

```c++
saveValue<Facet, Filter, ValCode, ValProps>(s, val);
loadValue<Facet, ZfFieldFilter::Load, ValCode, ValProps, Val>(node);
```

For non-UDT codes, these functions retain the existing JSON behavior for
strings, bytes, booleans, integers, floating point, fixed/decimal values,
timestamps, nulls, formatting properties, and vectors. Do not resolve
`As<Val>` for these scalar cases and do not introduce a second scalar
serialization path in `AsMap`.

Recursion occurs when `ValCode == ZfFieldTC::UDT`. The existing UDT branch of
`saveValue_`/`loadValue_` resolves
`As<Val>::Handler<Val, Facet>`, so a UDT value can map as `AsObject`, `AsArray`,
`AsString`, nested `AsMap`, or a custom compatible formatter. Forward `Facet`,
the applicable load/save filter, `ValCode`, and `ValProps` unchanged so nested
behavior remains compile-time selected and consistent with ordinary fields and
arrays.

Exercise at least these recursive forms:

- `AsMap` of `AsString` values;
- `AsMap` of `AsObject` values;
- `AsMap` of `AsArray` values, with `AsArray` still probing `[0]` for `Elem`;
- `AsMap` of `AsMap` values, where the nested value is itself a `ZmRef`;
- an `AsObject` field whose UDT mapping is `AsMap`.

Also exercise representative non-UDT values, including a JSON number and bool,
to prove they do not fall through `AsString` or the object-level ADL taxonomy.
Do not add run-time enums, type erasure, virtual dispatch, or a temporary
contiguous representation.

## 5. Add focused tests

Extend `zf/test/ZfStructTest.cc`, which already owns the JSON object-level
mapping tests, rather than adding a new test binary.

Define reference types for `ZmHash`, `ZmLHash`, and `ZmRBTree` containers with
string keys, `Val` types covering both scalar and recursive cases above, and ADL
`ZfJSON_Fmt` overloads returning the appropriately parameterized `AsMap`. Test:

- compile-time extraction of `Map`, `Key`, and `Val`, including the string-key
  assertion;
- empty-map save/load;
- key quoting and unquoting, including quotes, backslashes, and UTF-8;
- save followed by scan and `handler<MapRef>().ctor()` for all three container
  families;
- `handler<MapRef>().new_()` and load into an existing non-empty destination,
  asserting that the `ZmRef`/`Map` identity is retained while old entries are
  cleared;
- update of an existing map, asserting that the `ZmRef`/`Map` identity is
  retained, present keys are patched, absent keys are preserved, and new keys
  are added;
- non-UDT integer, bool, string/bytes, and formatted scalar values using the
  same JSON representation as ordinary fields;
- UDT values mapped as `AsString`, `AsObject`, `AsArray`, and nested `AsMap`;
- a map reference nested in a `ZfStruct` UDT field and an object nested as a map
  value;
- propagation of a non-default facet/filter through a nested `AsObject` value;
- null and wrong-kind map/value behavior matching ordinary `ZfStruct` member
  value loading, including `null` save and allocation on a valid object load;
- narrow-string key enforcement consistent with ordinary JSON string loading;
- duplicate JSON member names passed through in source order, with assertions
  based on each container's native duplicate-key/uniqueness behavior;
- iterator-order-independent assertions for `ZmHash`/`ZmLHash` (validate parsed
  entries or round trips rather than require textual order), while using
  deterministic `ZmRBTree` output for exact JSON assertions.

A negative non-string-key case should be documented by the handler's
`ZuAssert`; do not add an intentionally failing test to the normal build unless
the repository gains a compile-fail harness.

## 6. Verification sequence

After implementation:

1. Rebuild `ZfJSON` and its test binary: `make -C zf/src -j8` followed by
   `make -C zf/test -j8 ZfStructTest`.
2. Run `./zf/test/ZfStructTest` and confirm its TAP result.
3. Run `make -C zf/test test` to cover the other formats which include or reuse
   `ZfJSON.hh` (`ZfCf`, URI, CLI, YAML, and TOML).
4. Run the top-level `make test` when the configured dependency set is
   available.

## Open questions

None. The decisions above follow existing `ZfStruct` member-value behavior and
leave key uniqueness/duplicates to the selected Z container.
