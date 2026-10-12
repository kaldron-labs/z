# Zi: I/O and binary serialization

`zi` provides the framework's I/O facilities and FlatBuffers codec. Include
installed headers as `<zlib/ZiFB.hh>` and link with `libZi` and its framework
dependencies. This page documents FlatBuffers; see [zf](../zf/README.md) for
the shared field metadata and text/configuration codecs.

## FlatBuffers

**`ZiFB` is intended for latency-sensitive use cases**, alongside `ZfJSON`
and `ZfURI`. [ZiFB.hh](src/ZiFB.hh) uses compile-time field mapping, borrowed
wire views, and direct downward-buffer construction to minimize copying and
object materialization. Pooled I/O builders and transmit-stream integration
allow serialization into framework I/O storage.

### Define the wire mapping

Describe fields with `ZfStruct`, then associate a FlatBuffers type through ADL:

```cpp
#include <zlib/ZiFB.hh>

struct Record {
  unsigned value;
  ZuCSpan name;
};

ZfStruct(, (Record, FB),
  (value, (Ctor<0>), UInt32),
  (name,  (Ctor<1>), String));

ZiFB::AsTable<"example.Record"> ZiFB_Fmt(Record *);
```

The default facet is `ZuFacet::FB`; declare it with
`ZfStruct(API, (Type, FB), ...)`. Render another facet with
`ZfStructRender` and pass it to the codec when a separate wire field list is
needed. `ZiFB::AsStruct<"name">` describes inline structs; `ZiFB::AsUnion` and
`ZiFB::Alt` describe union alternatives. `ZiFB::AsEnum` names existing enum maps.

Size prefixing is configured per facet and defaults to false. For example:

```cpp
ZuStructFacet(Prefixed);
ZiFBConfig(Prefixed, ZiFB_SizePrefixed<>);
ZuStructReflect(Record, ZuFacet::Prefixed, ZuFacet::FB);

ZiFB::save<ZuFacet::Prefixed>(out, record);
auto copy = ZiFB::load<Record, ZuFacet::Prefixed>(out);
```

`ZiFB::Config<Facet>` resolves the `ZiFB_Config(Facet *)` ADL hook. `view`,
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
ZiFB::IOBuilder<> builder{new ZiIOBufAlloc<>};
auto bytes = builder.finish(ZiFB::save(builder, record));
if (!builder.failed() && ZiFB::verify<Record>(bytes)) {
  auto view = ZiFB::view<Record>(bytes);
  auto value = view.get<ZfField(Record, value)>();
  auto name = view.get<ZfField(Record, name)>();
  auto copy = ZiFB::handler<Record>(bytes).ctor();
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

Call `ZiFB::verify<Type>(bytes)` at an external input boundary before typed
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

### I/O and framing

`ZiFB::save(stream, object)` integrates with framework transmit streams and
returns a `ZiFB::Error` code. The size-prefix template argument and optional
file identifier select framing, for example `save<true>(stream, object,
"DATA")` and `verify<Type, true>(bytes, "DATA")`.

Prefer borrowed views when only a few fields are needed. Reuse builder storage
when lifetimes permit, tune the I/O buffer pool for the workload, and write
directly to the destination stream to avoid staging copies. Growth or a
splitting transmit path can still allocate or move bytes; the API does not
promise allocation-free serialization for every workload.

### Reflection and schema export

[ZiFBReflect.hh](src/ZiFBReflect.hh) supplies borrowed reflection access,
reflection-schema serialization, and schema export from compile-time metadata.
`ZiFB::saveSchema<Type>(stream)` emits BFBS;
`ZiFB::saveFBS<Type>(stream)` emits FBS text. Facet-specific schemas use the
selected metadata mapping.
This permits external FlatBuffers tooling to consume framework wire contracts
without making runtime schema discovery part of the ordinary typed path.

See [ZiFBTest.cc](test/ZiFBTest.cc) for codec and verification examples,
[ZiFBReflectTest.cc](test/ZiFBReflectTest.cc) for reflection and schema export,
and [ZiFBIOTest.cc](test/ZiFBIOTest.cc) for pooled ownership, growth, IP mapping,
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

`zi/src/zflatc` parses FBS schemas and generates native Z declarations and
`ZfStruct` metadata. It does not depend on canonical FlatBuffers or generate a
second wire codec. Build it with `make -C zi/src -j4`; the module's ordinary
`install-binPROGRAMS` target installs the executable.

```sh
zflatc fbs/foo.fbs                  # fbs/foo.hh and fbs/foo.cc
zflatc fbs/foo.fbs -o foo           # foo.hh and foo.cc in the current directory
zflatc --alt --ref ZmObject -I fbs -I shared foo.fbs
zflatc foo.fbs --export ZiAPI -o generated/foo
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
| Fixed arrays | Identified owning arrays with explicit `ZiFB::ArrayCount` extents |
| Enums / `bit_flags` | Canonical integer members, `ZtEnum` / `ZtFlags` maps |
| Unions | `ZuUnion` plus named `ZiFB::Alt` mappings |
| IDs, defaults, required/deprecated fields, alignment | Recovered `ZfStruct` / FB properties |
| Built-in `Zf` value schemas | Canonical 128-bit integers, fixed/decimal, time/date types, bytes and bitmap mappings |
| `Zi.IPv4`, `Zi.IPv6`, `Zi.IP` | `ZiFB::IPv4`, `ZiFB::IPv6`, and `ZiIP` |

The field-type inventory comes from `ZfFieldTC` and ZiFB's retained mappings:

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
Fixed arrays retain their extent through `ZiFB::ArrayCount`. Bitmap fields use
`ZtBitmap`; their wire form is the `Zf.Bitmap` table. Byte-array union payloads
use owning wrappers; the union's heap parameter receives the containing
member's heap ID. Multiple heap instantiations retain one logical wire union.
For built-in table roots, generated `Zf::Bytes` derives from an identified
owning byte array, and `Zf::Bitmap` derives from `ZtBitmap`.
`ZiFB::AsMapped` reuses the canonical table metadata, verifier and serializer;
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

`ZiFlatcTest` covers parsing/emission errors and replacement rollback.
[The integration fixtures](itest/README.md) generate FBS from real metadata,
compile generated code, compare normalized schema/metadata contracts and check
payloads in both directions, including ownership after source-buffer release.
