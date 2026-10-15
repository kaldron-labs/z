# Zfb: FlatBuffers serialization

`zfb` provides the FlatBuffers codec. Include `<zlib/Zfb.hh>` and link with
`libZfb`. It depends on `zi` for I/O and `zf` for field metadata.

## FlatBuffers

**`Zfb` is intended for latency-sensitive use cases**, alongside `ZfJSON`
and `ZfURI`. [Zfb.hh](src/Zfb.hh) uses compile-time field mapping, borrowed
wire views, and direct downward-buffer construction to minimize copying and
object materialization. Pooled I/O builders and transmit-stream integration
allow serialization into framework I/O storage.

### Define the wire mapping

Describe fields with `ZfStruct`, then associate a FlatBuffers type through ADL:

```cpp
#include <zlib/Zfb.hh>

struct Record {
  unsigned value;
  ZuCSpan name;
};

ZfStruct(, (Record, FB),
  (value, (Ctor<0>), UInt32),
  (name,  (Ctor<1>), String));

Zfb::AsTable<"example.Record"> Zfb_Fmt(Record *);
```

The default facet is `ZuFacet::FB`; declare it with
`ZfStruct(API, (Type, FB), ...)`. Render another facet with
`ZfStructRender` and pass it to the codec when a separate wire field list is
needed. `Zfb::AsStruct<"name">` describes inline structs; `Zfb::AsUnion` and
`Zfb::Alt` describe union alternatives. `Zfb::AsEnum` names existing enum maps.

Size prefixing is configured per facet and defaults to false. For example:

```cpp
ZuStructFacet(Prefixed);
ZfbConfig(Prefixed, Zfb_SizePrefixed<>);
ZuStructReflect(Record, ZuFacet::Prefixed, ZuFacet::FB);

Zfb::save<ZuFacet::Prefixed>(out, record);
auto copy = Zfb::load<Record, ZuFacet::Prefixed>(out);
```

`Zfb::Config<Facet>` resolves the `Zfb_Config(Facet *)` ADL hook. `view`,
`handler`, typed `verify`, and reflection verification use the same facet
configuration. `save<Facet, Filter>` has the same template argument order for
builders and streams; builder callers still control when and how to finish
an arena. Low-level `root`, verifier root access, and builder finishing retain
explicit framing controls.

`FB::FieldID<N>` supplies explicit wire field IDs, `FB::ID<"name">` overrides
the schema name, and `FB::Align<N>` requests alignment. Implicit IDs follow
field order. Explicit mappings must cover all fields consistently, remain
contiguous, and reserve the discriminator slot preceding a union field.
Metadata assertions diagnose invalid IDs, alignments, and name collisions.
The codec implements canonical FlatBuffers wire layout without requiring
generated C++ headers for ordinary framework use.

### Build, verify, and read

```cpp
Record record{42, "worker"};
Zfb::IOBuilder<> builder{new ZiIOBufAlloc<>};
auto bytes = builder.finish(Zfb::save(builder, record));
if (!builder.failed() && Zfb::verify<Record>(bytes)) {
  auto view = Zfb::view<Record>(bytes);
  auto value = view.get<ZfField(Record, value)>();
  auto name = view.get<ZfField(Record, name)>();
  auto copy = Zfb::handler<Record>(bytes).ctor();
}
```

`finish()` returns a span into the builder's storage. Check `failed()` and
`error()` for construction errors. `IOBuilder::buf()` transfers the backing
`ZiIOBuf` owner and resets the arena. Detaching can compact the completed frame;
obtain its span from the returned owner and retain that owner while borrowed
results are in use. Clearing, reusing, or growing a builder can invalidate
earlier spans.
`ArrayStorage` with `Builder` supports application-managed array storage when
pooled I/O is not the intended destination.

`save(builder, object)` deduces the object type and returns its offset; it does
not finish or detach the builder. Use `save<Facet, Filter>(builder, object)`
for explicit field selection. `save(pointer, wireStruct)` writes an inline
struct into supplied storage. These entry points select the nested format
handler, so ordinary callers do not name its type. `handler(unionView)` also
supports native union construction; `handler<Union, Facet>(unionView)` selects
the facet used to materialize its alternatives.

Call `Zfb::verify<Type>(bytes)` at an external input boundary before typed
access. Verification and access are explicit separate operations; `view()`
does not implicitly verify an arbitrary incoming buffer. The verification
APIs support resource limits, file identifiers, and size-prefixed framing.

`view<Type>()` reads fields directly from encoded storage, including nested
objects and vectors. `handler<Type>()` provides `ctor()`, `alloc()`, `load()`,
and `update()` for native objects when materialization is needed. Borrowed
strings, byte spans, and views require the backing bytes to remain alive and
valid for borrowed access. Constructing a native object with span fields does not copy their
contents; use owning field types when the result must outlive the buffer.

Native loading propagates the selected facet through nested objects, vectors,
fixed arrays, and union alternatives. Pointer members receive a newly allocated pointee via
its handler's `alloc()`, or null when absent; `ZuPtr` and `ZmRef` manage that
ownership, while callers must release raw-pointer results. Inline wire offsets
still come from the complete Core layout, regardless of the loading facet.

### Associative containers

Map formatting is selected through ADL, using the same container interface as
`ZfJSON::AsMap`: `Key`, `Val`, `citer()` with nodes exposing `key()`/`val()`,
and `add`, `del`, and `clean`. The format declaration is:

```cpp
Zfb::AsMap<Name, KeyCode, ValCode, KeyProps, ValProps> Zfb_Fmt(Container *);
```

`Name` is the qualified root-adapter name; the entry table appends `Entry`.
`KeyCode` and `ValCode` are `ZfFieldTC` codes. Both property lists default to
`ZuTypeList<>`; they supply ordinary field properties such as scalar defaults
and enum metadata. Keys use canonical scalar or string representations.
Values use the existing vocabulary conversions, recursive handlers and facets.
For example, an application can choose a value tree:

```cpp
ZuDerive(Counts, (ZmRBTreeKV<unsigned, ZtString<>,
  ZmRBTreeHeapID<"example.Counts">>));
Zfb::AsMap<"example.Counts", ZfFieldTC::UInt32, ZfFieldTC::String>
  Zfb_Fmt(Counts *);

struct Row { Counts counts; };
ZfStruct(, (Row, FB), (counts,, UDT));
Zfb::AsTable<"example.Row"> Zfb_Fmt(Row *);
```

Its wire shapes are:

```fbs
namespace example;
table CountsEntry { key:uint (key); value:string; }
table Counts { entries:[CountsEntry]; }
table Row { counts:[CountsEntry]; }
```

Map fields contain entry vectors directly; roots use the `entries` table
adapter. Nested maps use the same entry-vector representation in their value
field. Synthetic key/value/entries fields survive application filters and
facets, while nested application values retain the selected facet.
`zflatc` recognizes a table named `NameEntry` when it has exactly `key` at
ID 0 marked `(key)` and `value` at ID 1 (ID 2 for a union value). It emits a
native tree map for vectors of that entry, with an ADL `AsMap<Name,...>`
declaration. A generated field's map uses the field's heap ID. The table
`Name { entries:[NameEntry]; }` remains a normal generated root wrapper with
a native map in `entries`; the map can also be saved as a root through its
own `AsMap` handler. Generated `--ref` fields hold the map through `ZmRef`.
Keyed tables with other fields remain ordinary tables, preserving those
fields. `zflatc` rejects a conflicting `Name` table or a singular field
referring to `NameEntry`, where the map and ordinary table would claim the
same wire name. Schema imports keep their generated header guards and map
declarations. Header guards encode the complete output path, so similarly
named headers in different directories remain distinct.
The library makes no concrete-container ownership choice. Applications hold
`ZmHash` through `ZmRef` because it derives from `ZmPolymorph`; `ZmRBTree` may
be a value. Pointer traits select allocation for pointer-backed fields, and
ordinary field construction handles value-backed maps.

`save` constructs entries from node references, retains only offsets in
workload-sized scratch, and sorts the serialized keys for canonical
`LookupByKey`. Scalar comparisons and C-string comparisons match the wire
key representation. Borrowed access uses `handler<Counts>(bytes).entries()` or
`view<Row>(bytes).get<ZfField(Row, counts)>()`. The returned map view provides
`length()`, entry indexing, `key(i)`, `value(i)`, and `find(key)`; `find` returns
an entry view and assumes sorted input.

`load` clears the destination, and `update` replaces supplied keys while
retaining other entries. Both consume wire order and keep the last value for
a repeated key. Materialization accepts unsorted input. Existing structural
verification remains explicit at ingress; handlers do not validate ordering,
uniqueness, or key contents. `ctor()` requires a movable value container;
`alloc()`, placement `new_()`, and loading into an existing container support
reference-owned containers through the same handler.

Values with pointer traits may use raw pointers, framework smart pointers,
or application wrappers accepting implicit raw-pointer adoption. Replacement
and clearing release owned raw pointees exactly once, clearing their node
slots before node destruction. Smart pointers retain their own release
semantics. Final container destruction and the lifetime of borrowed fields
remain the application's responsibility; plain raw-pointer KV nodes do not
release their pointees automatically.

### I/O and framing

`Zfb::save(stream, object)` integrates with framework transmit streams and
returns a `Zfb::Error` code. The size-prefix template argument and optional
file identifier select framing, for example `save<true>(stream, object,
"DATA")` and `verify<Type, true>(bytes, "DATA")`.

Prefer borrowed views when only a few fields are needed. Reuse builder storage
when lifetimes permit, tune the I/O buffer pool for the workload, and write
directly to the destination stream to avoid staging copies. Growth or a
splitting transmit path can still allocate or move bytes; the API does not
promise allocation-free serialization for every workload.

### Reflection and schema export

[ZfbReflect.hh](src/ZfbReflect.hh) supplies borrowed reflection access,
reflection-schema serialization, and schema export from compile-time metadata.
`Zfb::saveSchema<Type>(stream)` emits BFBS;
`Zfb::saveFBS<Type>(stream)` emits FBS text. Facet-specific schemas use the
selected metadata mapping.
This permits external FlatBuffers tooling to consume framework wire contracts
without making runtime schema discovery part of the ordinary typed path.

See [ZfbTest.cc](test/ZfbTest.cc) for codec and verification examples,
[ZfbReflectTest.cc](test/ZfbReflectTest.cc) for reflection and schema export,
and [ZfbIOTest.cc](test/ZfbIOTest.cc) for pooled ownership, growth, IP mapping,
and transmit integration. Canonical FlatBuffers interoperability tests live in
[interop](interop/README.md).

## Build and tests

Configure with the repository's `z.config` wrapper and build dependencies with
top-level `make -j4`, then run:

```sh
make -C zi/test -j4
make -C zi/test test
```

[test/README.md](test/README.md) describes the test groups and negative metadata
compilation checks.

## zflatc

`zfb/src/zflatc` parses FBS schemas and generates native Z declarations and
`ZfStruct` metadata. It does not depend on canonical FlatBuffers or generate a
second wire codec. Build it with `make -C zfb/src -j4`; the module's ordinary
`install-binPROGRAMS` target installs the executable.

```sh
zflatc fbs/foo.fbs                  # fbs/foo.hh and fbs/foo.cc
zflatc fbs/foo.fbs -o foo           # foo.hh and foo.cc in the current directory
zflatc --alt --ref ZmObject -I fbs -I shared foo.fbs
zflatc foo.fbs --export ZfbAPI -o generated/foo
```

Options can precede or follow the schema. `--alt` selects `.hpp`/`.cpp`.
Repeated `-I` directories are searched in their command-line order after the
including schema's directory. Included schemas are parsed for resolution;
only the requested schema is generated. An include of `bar.fbs` becomes
`#include "bar.hh"`, or `"bar.hpp"` with `--alt`; generate `bar` separately.
`--export` decorates declarations and metadata with the supplied macro; it is
empty by default. Consumers provide any custom export macro or reference base
through their component headers/build configuration.
Reference bases accept qualified C++ names, including a leading `::`;
`--ref ::flatcref::Base` selects a consumer-provided base. Names that hide the
intrusive base's `ref`, `deref`, `mvref`, `refCount`, or `withRef` interface are
rejected under `--ref`, before either output is replaced.

| FBS construct | Generated native representation / metadata |
| --- | --- |
| `bool`, integer scalars | `bool`, canonical width/signed integer types |
| `float`, `double` | `float`, `double`; `Float` metadata preserves wire width |
| `string` | Named `ZuDerive` over an identified `ZtString` |
| Primitive/string vectors | Named identified `ZtArray`, with owning string elements |
| Tables and table vectors | `ZuPtr<T>` and arrays of `ZuPtr<T>` |
| `--ref <type>` | `ZmRef<T>` references; native structs derive from the selected base |
| Inline structs | Native values; Core handler determines wire layout |
| Fixed arrays | Identified owning arrays with explicit `Zfb::ArrayCount` extents |
| Enums / `bit_flags` | Canonical integer members, `ZtEnum` / `ZtFlags` maps |
| Unions | `ZuUnion` plus named `Zfb::Alt` mappings |
| IDs, defaults, required/deprecated fields, alignment | Recovered `ZfStruct` / FB properties |
| Built-in `Zf` value schemas | Canonical 128-bit integers, fixed/decimal, time/date types, bytes and bitmap mappings |
| `Zi.IPv4`, `Zi.IPv6`, `Zi.IP` | `Zfb::IPv4`, `Zfb::IPv6`, and `ZiIP` |

The field-type inventory comes from `ZfFieldTC` and Zfb's retained mappings:

| Field typecodes | Wire representation | Generated member / element |
| --- | --- | --- |
| `CString`, `String` | `string` | Identified owning `ZtString` wrapper |
| `Bytes` | `[ubyte]` | Identified owning `ZtArray<uint8_t>` wrapper |
| `Bool` | `bool` | `bool` |
| `Int8`/`UInt8` through `Int64`/`UInt64` | Corresponding FBS integer | Corresponding canonical integer |
| `Int128`, `UInt128` | `Zf.Int128`, `Zf.UInt128` inline values | `int128_t`, `uint128_t` |
| `Float` | `float` or `double` | Matching wire-width floating type |
| `Fixed`, `Decimal` | `Zf.Fixed`, `Zf.Decimal` inline values | `ZuFixed`, `ZuDecimal` |
| `Time`, `DateTime` | `Zf.Time`, `Zf.DateTime` inline values | `ZuTime`, `ZuDateTime` |
| `CStringVec`, `StringVec` | `[string]` | Identified arrays of identified owning string wrappers |
| `BytesVec` | `[Zf.Bytes]` | Identified arrays of identified owning byte wrappers |
| Integer, `Float`, `Fixed`, `Decimal`, `Time`, `DateTime` vector codes | Vectors of the corresponding wire value | Identified arrays of the corresponding native type |
| `UDT` | Declared table, inline value, union, or mapped built-in | Selected owning table reference, native inline value, `ZuUnion`, or canonical built-in |

Container materialization uses the existing value conversions; nested objects
use facet-aware handlers. The typecode list has no boolean-vector code.
Fixed arrays retain their extent through `Zfb::ArrayCount`. Bitmap fields use
`ZtBitmap`; their wire form is the `Zf.Bitmap` table. Byte-array union payloads
use owning wrappers; the union's heap parameter receives the containing
member's heap ID. Multiple heap instantiations retain one logical wire union.
For built-in table roots, generated `Zf::Bytes` derives from an identified
owning byte array, and `Zf::Bitmap` derives from `ZtBitmap`.
`Zfb::AsMapped` reuses the canonical table metadata, verifier and serializer;
its native factories/load path apply the existing value conversion. Canonical
bytes/bitmap metadata declares matching Core and FB facets. No duplicate
built-in wire structs or metadata implementations are generated.
Roots supplied by included schemas receive the same owning adapter. A guard
by canonical wire name prevents duplicate adapters through indirect includes.

The parser follows the Zf span-scanning conventions (`bok`, `eok`, `eon`,
`eos`), retains borrowed tokens in stable file buffers, and uses `ZuMatcher`
for keyword choices. Integer conversion uses `ZuBox`; hexadecimal/exponent
floating defaults are range-checked with the C floating scanner because
`ZuBox` does not consume those forms. Unicode string escapes use Z UTF helpers.

Field names must follow snake-case and become camel-case C++ members. Keywords,
reserved/overlong names and converted-name collisions are errors. `Zflatc_` is
reserved for generated implementation namespaces, whose helpers are scoped by
public type so separately generated dependencies can share a namespace.
Enum names reserved by `ZtEnum`/`ZtFlags` and overlong flag ordinal names are
also rejected. Native type name `Base` is reserved by `ZuDerive`; live members
cannot have their containing type's name. Schema namespace components, type
names and enum/union alternatives beginning with `Z` followed by a lowercase
letter are reserved for framework names. The recognized canonical `Zf`/`Zi`
value declarations remain supported with their exact validated wire shapes.
This schema restriction does not apply to `--ref` bases or `--export` macros.
Container metadata uses its implementation scope when the public type's
constructor name hides an inherited container alias. Each member's
owning string/array storage uses the heap ID `"qualified.Type.member"`, using
its C++ member spelling. Inner string/byte-array elements use the same ID,
without a suffix. Allocated structs use the concrete-type `ZmHeap` pattern.
Generated code declares Core and canonical FB facets; source-specific facets
and non-FBS metadata are not reconstructed.

FBS representations are not always unique: C strings and string spans have the
same schema, as do byte spans and unsigned-byte vectors. Generation chooses
owning strings and byte arrays for these contracts. Arbitrary application
transforms cannot be recovered. Supported schema constructs follow Z's FBS
emitter; unrelated FlatBuffers language features are diagnosed as out of scope.

Validation finishes before outputs are touched. Both files are staged and
closed before replacement. Handled replacement errors restore the previous
pair, retaining a backup if restoration itself fails. Replacing two separate
paths is not crash-atomic.

`ZfbFlatcTest` covers parsing/emission errors and replacement rollback.
[The integration fixtures](itest/README.md) generate FBS from real metadata,
compile generated code, compare normalized schema/metadata contracts and check
payloads in both directions, including ownership after source-buffer release.
