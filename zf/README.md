# Zf: structured data and formats

`zf` supplies field metadata, serialization, command-line binding, and
configuration parsing. Include the installed headers as `<zlib/ZfJSON.hh>`
(and similarly for other components) and link with `libZf` and its framework
dependencies. The library is not header-only.

**JSON and URI are intended for latency-sensitive use cases.** They use
compile-time field dispatch and formatting, decode into mutable input in place,
and support borrowed values to avoid unnecessary copies. FlatBuffers has the
same latency-sensitive intent; its implementation and usage are documented in
[Zi's README](../zi/README.md#flatbuffers).

## ZfStruct and its format bindings

### ZfStruct: describe the object first

[ZfStruct.hh](src/ZfStruct.hh) extends `ZuStruct` with typed field metadata,
defaults, bounds, keys, formatting, and type-erased runtime introspection.
Describe an existing C++ type; the macro does not define its storage:

```cpp
#include <zlib/ZfStruct.hh>

struct Config {
  unsigned value = 4;
  ZtString<> role{"server"};
};

ZfStruct(, Config,
  (value, (Mutable, (Range<0U, 8U>)), UInt32),
  (role,  (Mutable),                String));
```

The first macro argument is an export annotation, empty for local types.
Each field is `(accessor, properties, typecode)`; retain the properties position
even when empty. Typecodes include `String`, `Bytes`, `Bool`, sized integers,
`Float`, `Fixed`, `Decimal`, `Time`, `DateTime`, `UDT`, and vector variants.
Accessors can bind members, methods, or lambdas. A `Skip` accessor declares
metadata without requiring a corresponding C++ member.

Common properties include `Ctor<N>` for constructor arguments, `Mutable` for
updates, `Keys<N>` for keys, `Deflt<V>` for a canonical default, `Required`,
`Range<Min, Max>`, and enum/flags maps. Constructor-bound fields are passed in
`Ctor` order; other writable fields are initialized through their accessors.
`load()` and `update()` have different field filters: updates select mutable
fields and primary-key fields, so mark fields deliberately rather than assuming
every field will be overwritten.

A facet is a compile-time view of the fields. `ZfStruct` always defines the
`Core` facet; `(Config, JSON, TOML)` also exposes those format facets. To select,
reorder, rename, or add format properties to a subset, include that format's
header and render a facet explicitly:

```cpp
#include <zlib/ZfJSON.hh>

ZfStructRender(, Config, JSON,
  value,
  (role, (JSON::ID<"mode">)));
```

Canonical format APIs default to their named facet, such as `ZuFacet::JSON`.
Pass `ZuFacet::Core` explicitly when using the original field list. Define
custom facets with `ZuStructFacet(Name)` and pass `ZuFacet::Name` to the codec.
`ZfStructImpl(Type[, Facet])` supplies the out-of-line field matcher when using
runtime matching. Compile-time introspection uses `ZfField(Type, field)` and
`ZuFields<Type, Facet>`; runtime consumers use `ZfVFields<Type>()` and
`ZfVKeyFields<Type>()` without virtual field objects or RTTI dispatch.

Format-wide tuning follows the facet through nested values. Declare a facet
and bind its configuration at global scope; the macro places the hook in the
facet's namespace so argument-dependent lookup selects it:

```cpp
ZuStructFacet(Wire);
ZfASN1Config(Wire, (
  ZfASN1_StashSize<16, ZfASN1_OIDSize<8, ZfASN1_VecSize<4>>>));
```

Render or reflect the object's fields onto `ZuFacet::Wire`, then pass that
facet to the format API. `Format::Config<Facet>` resolves the configuration.
The defaults and overrides are:

| Format | Configuration macro | Overrides (defaults) |
| --- | --- | --- |
| ASN.1 | `ZfASN1Config` | `ZfASN1_StashSize` (32), `ZfASN1_OIDSize` (16), `ZfASN1_VecSize` (8) |
| CBOR | `ZfCBORConfig` | `ZfCBOR_VecSize` (8) |
| CSV / ZiCSV | `ZfCSVConfig` | `ZfCSV_MaxRowLen` (4096) |
| TOML | `ZfTOMLConfig` | `ZfTOML_ScalarScratchSize` (128), `ZfTOML_PathScratchSize` (128) |

Each override takes its capacity followed by an optional nested configuration.
ASN.1, CBOR, and TOML capacities tune inline scratch storage and permit heap
fallback. CSV's maximum row length is a hard bound: overflow retains the
existing writer failure and reader error behavior. `ZiCSV` shares the CSV
facet configuration for file readers and writers. ASN.1 `save`, CSV `write`,
and ZiCSV file APIs no longer take positional capacity template arguments.

The following formats build on this metadata. Where supported, handlers offer
`ctor()` to construct a value, `alloc()` to allocate one, `load(existing)` to
load writable fields, and `update(existing)` to apply the update filter.
`saveUpd()` and `saveDel()` select update and deletion fields for output.
Check each codec's parse/handler result before accessing data; their error
contracts differ.

### JSON and URI

#### JSON

[ZfJSON.hh](src/ZfJSON.hh) provides structured JSON load/save with compile-time
field matching. For the `Config` and JSON facet above:

```cpp
ZtString<> input{"{\"value\":8,\"mode\":\"client\"}"};
auto parsed = ZfJSON::scan(input.span());
if (parsed.p<0>() >= 0) {
  auto h = ZfJSON::handler<Config>(parsed.p<1>().ptr());
  if (h.valid) {
    Config config;
    h.load(config);
    ZtString<> output;
    ZfJSON::save(output, config);
  }
}
```

`scan()` returns a consumed offset and a parse tree; a negative offset indicates
failure. Check the consumed length if your application requires exactly one
document. The parser accepts unquoted keys. String unquoting and byte decoding
mutate input; use writable storage and keep it alive while the parse tree or
loaded borrowed spans refer to it. The tree owns nodes, not the underlying
input text. Owning destination strings/arrays can copy values when independent
lifetimes are needed.

Use `JSON::ID`, `Opt`, byte encodings (`Base64`, `Base64URL`, `Base32`, `Hex`,
`Raw`), and number/time formatting properties to control fields. UDTs with
fields default to object formatting; other UDTs default to strings. ADL
`ZfJSON_Fmt(T *)` selects `AsArray<typecode>`, `AsMap<typecode>`, `AsString`,
or a custom handler. `ZfJSON::Union<Ts...>` supports discriminated values.

The latency design avoids repeated runtime schema discovery and unnecessary
string copies. `scan()` still allocates parse-tree nodes; it is not an
allocation-free parser. Save directly to the intended output stream/buffer
when an intermediate string would add copying.

#### URI

[ZfURI.hh](src/ZfURI.hh) binds typed fields to URI paths, query strings, and
form bodies. Use `URI::PathIndex<N>` for path positions and `URI::ID<"name">`
for query names. Render the `URI` facet, then call `ZfURI::save(out, object)`
for the combined representation, `savePath()` for path fields, or `saveBody()`
for an encoded query/form body.

For general input, call `ZfURI::scan(mutableSpan)` and pass the returned tree
to `ZfURI::handler<Type>(node)`. Check the consumed offset and handler validity
as for JSON. URI decoding is also in place; borrowed fields require the input
buffer to remain alive. `ZfURIConfig` controls array/object flattening,
delimiters, quoting, and boolean conventions. ADL `ZfURI_Fmt(T *)` can select
`AsObject`, `AsArray`, `AsString`, or `AsJSON` for embedded JSON values.

For latency-sensitive routing, `ZfURI::loadPath(object, mutableSpan)` loads
typed path fields directly without constructing a parse tree and returns
success as `bool`. Supply only the path, excluding any `?query` suffix.
`loadPathPrefix()` loads leading typed fields and returns the remaining path
beginning with `/`; a null span means failure. These direct paths retain the
same writable-buffer and borrowed-value lifetime requirements.

See [ZfURITest.cc](test/ZfURITest.cc) for path, query, and embedded JSON examples.

### CBOR

[ZfCBOR.hh](src/ZfCBOR.hh) provides binary structural load/save. Render a
`CBOR` facet and use `ZfCBOR::save(out, object)`. Validate input with
`ZfCBOR::scan(bytes)` (a `Result` convertible to `bool`), then use
`ZfCBOR::handler<Type>(bytes)` to construct or load the object.

Object fields normally use text keys; `CBOR::ID<"1">` with `CBOR::IntID`
selects an integer key. Properties also support tags and simple values.
ADL `ZfCBOR_Fmt(T *)` selects object, array, map, or string representations.
Input is borrowed binary storage; retain it for borrowed strings/bytes and
views. See [ZfCBORTest.cc](test/ZfCBORTest.cc) for integer keys, tags, vectors,
and round trips.

### ASN.1

[ZfASN1.hh](src/ZfASN1.hh) maps fields to ASN.1 binary encodings. Use
`ASN1::Type<...>` and `ASN1::Fmt<...>` properties with the utilities in
`ZfASN1::Encoding` to specify types, tag classes, tags, and constructed forms.
The facet determines the wire mapping:

```cpp
ZfASN1::save<ZuFacet::Wire>(out, object);
auto h = ZfASN1::handler<Type, ZuFacet::Wire>(input);
auto copy = h.ctor();
```

Here `Wire`, `Type`, `out`, and `input` stand for the application's declared
facet, object type, output buffer, and encoded input. Preserve encoded storage
for any borrowed results. [ZfASN1Test.cc](test/ZfASN1Test.cc) demonstrates
tagging, OIDs, timestamps, vectors, construction, and allocation.

### CSV

[ZfCSV.hh](src/ZfCSV.hh) supplies a streaming row API with metadata-bound
columns. `ZfCSV::Writer<Type, Facet>` writes headers with `saveHdr(out)` and
rows with `save(out, object)`; its column list selects and orders output.
`CSV::ID<"column">` changes a column name, and CSV byte properties select
binary-text encodings.

`ZfCSV::Reader<Type, Facet>` binds the input header to fields and invokes a
callback for each row through `read(mutableSpan, callback)`. Inside the callback,
use the row's `ctor()`, `load()`, or `update()` operations. Row scratch storage
is temporary and quoting may mutate input; consume the row during the callback
and retain or copy borrowed cell data as appropriate. See
[ZfCSVTest.cc](test/ZfCSVTest.cc). File/stream integration is in `ZiCSV`.

### D-Bus

[ZfDBUS.hh](src/ZfDBUS.hh) serializes D-Bus structural values and message
bodies. Render a `DBUS` facet; override wire types with `DBUS::Type` and
`DBUS::ElemType` where necessary. `ZfDBUS::save(out, object, context)` uses
the context's byte order and starting offset for wire alignment.

Construct `ZfDBUS::handler<Type>({bytes, signature, offset, order})` and test
the handler before loading. `result()` exposes an error and offset. Both the
encoded bytes and signature must remain immutable for the handler's lifetime;
borrowed results also depend on the encoded storage. `saveMeasured()` supports
output using a previously measured layout. This is a codec; D-Bus connections
and message transport belong to [zdbus](../zdbus/README.md).
See [ZfDBUSTest.cc](test/ZfDBUSTest.cc) for arrays, dictionaries, variants,
signatures, and both byte orders.

### TOML, YAML, and Cf configuration

[ZfTOML.hh](src/ZfTOML.hh), [ZfYAML.hh](src/ZfYAML.hh), and
[ZfCf.hh](src/ZfCf.hh) share the owned configuration tree in
[ZfTree.hh](src/ZfTree.hh) and typed loading policies in
[ZfTreeLoad.hh](src/ZfTreeLoad.hh). Their scanners accept immutable text and
store configuration values in the tree. Borrowed loaded values depend on the
tree's lifetime rather than the original input buffer.

They share the scan, handler, and save pattern. With the original `Config`
metadata above, explicitly select `Core`:

```cpp
#include <zlib/ZfTOML.hh>

auto parsed = ZfTOML::scan("value = 8\nrole = 'client'\n");
if (parsed.p<0>() >= 0) {
  Config config;
  ZfTOML::handler<Config, ZuFacet::Core>(parsed.p<1>().ptr()).update(config);
  ZtString<> output;
  ZfTOML::save<ZuFacet::Core>(output, config);
}
```

Use `ZfYAML` with YAML text or `ZfCf` with Cf text for the corresponding
operations. Typed loading reports missing required fields, invalid types,
and range/enum failures through `ZeException`. Updates preserve absent fields.
Use format facets when each representation needs different names or properties.

- **TOML:** tables, arrays of tables, inline tables, typed scalars, and date/time
  values. `TOML::Inline` and scalar styles such as `Basic`, `Literal`, and
  multiline variants control output. `Scan::error()` reports syntax location
  and reason; `Limits` bounds parser depth and node count.
- **YAML:** block/flow mappings and sequences, scalar styles, anchors, and
  aliases composed into an owned tree. Directives, tags, merge/complex keys,
  and shared/cyclic graphs are rejected. Use YAML properties to select
  presentation. `Scan` exposes syntax diagnostics and accepts parsing limits. See
  [ZfYAMLTest.cc](test/ZfYAMLTest.cc) for the supported syntax and formatting.
- **Cf:** the framework's configuration syntax, including nested values,
  definitions, directives, and percent substitution through `PctFn` and
  `Defines`. The free `scan()` function throws on syntax, directive, or
  trailing-input failure. See [ZfCfTest.cc](test/ZfCfTest.cc).

`ZfTree::AnyNode::resolve(path)` locates configuration nodes and `path(out)`
formats their location for diagnostics. Use these facilities when the schema
is dynamic; use typed handlers when the configuration shape is known.
[ZfConfigTest.cc](test/ZfConfigTest.cc) demonstrates consistent typed updates
and range validation across the three formats and CLI.

## CLI

[ZfCLI.hh](src/ZfCLI.hh) binds options and positional arguments to metadata.
Use `CLI::Long<"value">`, short-option and positional properties to describe
arguments, and select the `CLI` facet or pass another facet explicitly.
For example, add `CLI::Long<"value">` to the `value` field above, then load
the original field list with `ZfCLI::load<ZuFacet::Core>(config, argc, argv)`.

For explicit parsing, create `ZfCLI::Parser<Type, Facet>`, call
`scanArgv(spanArgv)`, then use `ZfCLI::handler<Type, Facet>(parser.root)`.
Errors are reported as `ZeException`. `InArgv` adapts `argc`/`argv`, `InCLI`
parses command text, and `OutArgv` owns generated arguments. `save()` emits
command text; `saveArgv()` emits an argument vector. Include the program name
as argument zero when constructing an argument list.

`ZfCLIConfig` controls array/object flattening and delimiters. ADL
`ZfCLI_Fmt(T *)` selects strings, objects, arrays, or embedded JSON; embedded
JSON uses the selected CLI facet. Input decoding may mutate argument storage;
the default assumes writable `argv`. Build with `ZfCLI_MutableArgv=0` for
platforms with immutable arguments. Preserve the backing storage of borrowed
values. See [ZfCLITest.cc](test/ZfCLITest.cc) for complete examples.

## Build and tests

Configure through the repository's `z.config` wrapper and build dependencies
with top-level `make -j4`. The format and metadata tests live in `zf/test`:

```sh
make -C zf/test -j4
make -C zf/test test
```

See [test/README.md](test/README.md) for test scope. FlatBuffers codec,
reflection, and pooled I/O tests live in `zi/test`.
