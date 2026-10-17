# Zpb integration tests

## Go interoperability

`zpbgotest` launches the independent [Go protobuf peer](../interop/README.md#go-interoperability)
from `zpb/interop/go`. It exports native `ZfStruct` schemas as descriptors,
sends native wire payloads, and verifies and loads independently constructed Go
replies. The fixture checks uint64, ZigZag, floating-point, strings, binary
bytes, packed signed integers and bools, nested messages, maps, message-valued
`oneof` switches, and unknown fields in both directions.

Both raw protobuf and varint-delimited I/O are covered. A delimited sequence
contains an empty message and payload sizes 127, 128, 16383 and 16384, crossing
the one/two/three-byte prefix boundaries. The Go peer rejects truncated
prefixes, truncated payloads, overflowing varints and messages above its
configured size limit. Process execution has a timeout; `ZiTestResidue` retains
failed fixtures and peer diagnostics under the usual test log directory.

When Go is configured, the normal `all` target builds the peer and driver,
and `make test` runs the driver alongside the generator integration tests:

```sh
make -C zpb/src -j4
make -C zpb/itest -j4
make -C zpb/itest test
```

The process/file helpers in `ZpbTestUtil.hh` are shared with `zprotoctest`.

## zprotoc integration

`zprotocconsumer` compiles the generated `zprotoc_simple.proto` message in a
separate translation unit and exchanges protobuf bytes with a handwritten
`Zpb` type, including a finite `Row` → `Extra` → `Row` reference chain and
repeated owning message pointers. It also exercises nested messages and enums,
including singular and repeated enum fields with sparse and negative numeric
values.
The generated consumer also covers message-valued `oneof` alternatives,
including an all-`oneof` message and replacement of the active alternative.
It covers generated maps with scalar, string, bytes, enum and message values,
signed and fixed wire forms, and last-value-wins duplicate keys. The bytes
map uses owning generated storage and an independent duplicate-key wire case.
Its `FloatRow` fixture round trips repeated `float` and `double` values and
checks that native schema re-export preserves their distinct protobuf types.
Its `NumericRow` fixture compiles all standard protobuf numeric scalar types
and round trips representative repeated varint, ZigZag, fixed-width, and
floating-point fields.
`zprotocimport` compiles a generated importer and dependency in separate
translation units, then saves, verifies, and loads singular and repeated
imported message and enum fields, including an imported nested message.
It also covers an imported message-valued map. Both fixture schemas are proto2,
so canonical `protoc` accepts their imported enum fields.
`zprotoctest` invokes the standalone generator and checks repeatable
text and descriptor-set output for forward, recursive, and imported message references,
including an imported canonical `Zf.Fixed` used as a singular, repeated, and map
value. It checks that renaming a text or descriptor schema input leaves
generated C++ unchanged when the output stem and schema contents are unchanged.
Standard unrelated file options likewise leave generated C++ unchanged;
custom file options and quoted boolean/enum option values fail with a
diagnostic before output replacement.
Malformed built-in descriptor mappings preserve prior output, and
built-in-only imports do not require generated dependency headers. It
verifies rejection of unsupported schema syntax,
duplicate fully qualified text declarations,
invalid generated C++ type or package names in reachable imports,
proto2 enum fields and map values used by a proto3 message,
unsupported Editions, import qualifiers, services, extension ranges, groups,
and field options in text; descriptor input now reports recognized unsupported
features such as services before replacing output,
and checks that a handled output failure preserves existing files. Build and run just these
Zpb targets with the configured Clang debug build:

```sh
make -C zpb/src -j4 zprotoc
make -C zpb/itest -j4 zprotoctest zprotocimport
./zpb/itest/zprotoctest
./zpb/itest/zprotocimport
```

`zprotocvalues` compiles the six numeric/temporal mapped-value schemas, the
canonical address and bitmap schemas, and an importing `ValueRow`. It checks
singular, repeated, and map values, plus payload exchange with handwritten
mappings. Invalid canonical value shapes fail in `zprotoctest` before output
replacement.

```sh
make -C zpb/itest -j4 zprotocvalues
./zpb/itest/zprotocvalues
```

The CLI is `zprotoc [-I dir]... [-o stem] [--syntax proto2|proto3]
[--export macro] name.proto` or `zprotoc --descriptor [--file name]
[-o stem] [--syntax proto2|proto3] [--export macro] set.pb`;
`-o` writes `stem.hh` and `stem.cc`. The supported proto2/proto3 grammar
includes packages, imports, messages, enums, scalar fields, repeated fields,
maps, and message-valued `oneof`. Proto2 labels, supported defaults, packed
options, and reservations are retained. File and nested enums retain explicit
signed 32-bit numbers. Nested message declarations use flattened C++ names.
Message-valued `oneof` groups
generate `ZuUnion`/`AsUnion` declarations; scalar alternatives and cyclic
by-value alternatives are rejected. Maps generate `ZmRBTreeKV`/`AsMap`
declarations for supported key and value types. Generated strings,
bytes, and repeated values own heap identified storage. Text imports search
the source directory, then each `-I` directory. Generate each dependency header
under its imported `.proto` path with a `.hh` suffix so the include resolves.
The compiled proto3 consumer exercises two independent `oneof` groups in one
message, including interleaved wire fields and a switch in each group.
Package and import declarations can appear on either side of message/enum
declarations; a second package declaration fails before output replacement.
Descriptor input uses the same semantic model and defaults to its last file.
When supplied, `--syntax` checks the selected input file's declared version
before publication; it does not enter generated C++ metadata.
Proto2 `optional`, `required`, and `repeated` fields also generate. Decimal
integer, floating-point (including infinities), bool, enum, escaped string
(including Unicode), and escaped byte defaults initialize native members and are retained in
generated metadata. `zprotocproto2` checks required presence, round trips,
native defaults, a packed repeated `sint32` field, and a message-valued
proto2 `oneof`. Its choice fixture
rejects malformed wire in a discarded alternative, accepts a discarded
alternative missing a required field, and saves only the final alternative.
The same generated type combines that `oneof` with a map: an independent wire
fixture repeats a key, the last value wins, native save/load retain both fields,
and proto2 schema export retains the map and `oneof` metadata. The exported
descriptor verifies and emits both declarations as `.proto` text.
Omitting the `syntax` declaration selects proto2, including when `--syntax
proto2` checks that choice.
NaN defaults are rejected as native null sentinels.
Proto2 reserved numbers/ranges and names parse from text and descriptor sets;
generated `AsMessage` metadata preserves them on later native schema export.
The driver rejects proto3 optional fields from text and descriptor inputs
with an explicit diagnostic before replacing prior output. Reflection still
imports and emits those descriptors. Generated C++ uses canonical native types
for the schema: C-string ownership, narrow integer widths, fixed bitmap width,
and application facets are not recoverable from `.proto` input.
