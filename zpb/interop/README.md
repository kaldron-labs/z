# Zpb interoperability

## Zpb canonical interoperability

`zpbinterop` links the canonical protobuf library only in this explicit
interop target. It parses native-exported schemas with canonical tooling and
checks typed native map/`oneof` bytes in both directions using dynamic
protobuf messages. The canonical-to-native case also appends an unknown field
and confirms that native loading drops it. `zprotoc` also generates a small
map/`oneof` fixture compiled into this target; it exchanges bytes with canonical
dynamic messages in both directions. Generated built-in value schemas now
also compile into the target and exchange a `ValueRow` with canonical dynamic
protobuf in both directions. The same program checks canonical correctness
for the four `ZpbBench` workload shapes without running comparative timings.
Build and run only this Zpb target with the configured Clang debug build:

```sh
make -C zpb/interop -j4 zpbinterop
./zpb/interop/zpbinterop
```

Build canonical protobuf and its Abseil dependencies with the same compiler,
C++ language mode and standard library as the Z consumer. In particular,
[Clang 18 changed dependent-template name mangling](https://releases.llvm.org/18.1.0/tools/clang/docs/ReleaseNotes.html#c-specific-potentially-breaking-changes).
Older Abseil binaries can therefore leave unresolved `LogMessage` symbols when
linked from current Clang in GNU C++23 mode. Rebuild the dependencies together;
do not change Z's ABI flags to accommodate those binaries. An isolated install
prefix avoids replacing dependencies used by unrelated programs.
Point `PKG_CONFIG_PATH` at that prefix when running `z.config`. For static
canonical libraries, also set `PB_INTEROP_LIBS` to `pkg-config --static --libs
protobuf` so their transitive dependencies are included. These flags affect
only the explicit C++ interop target.

For example, after installing a matching static dependency build:

```sh
pb_prefix="$HOME/.local/zpb-interop"
export PKG_CONFIG_PATH="$pb_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
PB_INTEROP_CFLAGS="-isystem $pb_prefix/include" \
PB_INTEROP_LIBS="$(pkg-config --static --libs protobuf)" \
  ./z.config -L -d /usr
```

The repaired dependency build uses protobuf 25.3 and Abseil 20230802.1 with
Clang 21 and CMake's `CMAKE_CXX_STANDARD=23`, `CMAKE_CXX_EXTENSIONS=ON` and
`ABSL_PROPAGATE_CXX_STD=ON`. `-isystem` keeps third-party-header diagnostics
separate from repository source diagnostics.

`testDelimited` also exchanges varint-prefixed messages with canonical C++
delimited-message helpers, covering empty messages and prefix-width boundaries.

## Go interoperability

`go/zpbpeer` uses `google.golang.org/protobuf` (pinned in `go.mod` and `go.sum`)
to import native-exported descriptors and independently check and construct
messages. It uses `proto` for raw messages and `protodelim` for varint-prefixed
message sequences. No Go code generator or canonical C++ library is needed.
Go 1.23 or later is required; configuration's existing `HAVE_GO` condition
controls the peer and its TAP integration driver, `zpb/itest/zpbgotest`.

```sh
make -C zpb/src -j4
make -C zpb/itest -j4
make -C zpb/interop test-go
```

`make -C zpb/interop -j4` builds the Go peer alongside enabled canonical C++
targets. `make -C zpb/interop test` runs both peer suites when their dependencies
are configured. The Go suite also runs through `make -C zpb/itest test`.
See [the integration-test coverage](../itest/README.md#go-interoperability)
for fixtures and malformed-input cases. The first Go build downloads the pinned
module dependencies through the normal Go module cache.


---

## Zpb canonical interoperability

`zpbinterop` is an explicit Zpb target in `zpb/interop`. It is built when
canonical `protobuf` development files are available; production `libZpb` does
not link them. In the Clang debug build, run:

```
make -C zpb/interop -j4 zpbinterop
./zpb/interop/zpbinterop
```

For the Zpb memory check, run from the repository root:

```
./libtool --mode=execute valgrind --leak-check=full \
  --errors-for-leak-kinds=definite,indirect,possible \
  --suppressions=zpb/interop/zpbinterop.valgrind.supp \
  --error-exitcode=99 ./zpb/interop/zpbinterop
```

The program calls `google::protobuf::ShutdownProtobufLibrary()` after its
fixtures. The suppression matches only the remaining possible-leak allocation
in canonical protobuf's parser type-name table; definite and indirect leaks,
other possible leaks, and memory errors remain unsuppressed. The Clang debug
run passed 26 TAP subtests with zero unsuppressed Valgrind errors.

The fixture emits `base.proto` and `user.proto` from Zpb descriptor metadata.
Canonical protobuf parses both texts and builds descriptors for a direct import,
map entry, and message-valued `oneof`. A Zpb payload is read by a canonical
dynamic message. Canonical descriptor bytes are imported by Zpb reflection,
and canonical re-serialized payload bytes pass Zpb verification. The fixture
uses canonical protobuf 36.1 in the recorded Clang debug run.

The bool-vector fixture exports both unpacked and packed native writers.
Canonical protobuf builds the emitted proto3 schemas, reports the selected
`is_packed()` value, and reads the corresponding native wire. A canonical
descriptor set containing `packed = true` imports through Zpb reflection.

The native map fixture exports a `ZfStruct` message containing a map and
`oneof`, emits `.proto`, and has canonical protobuf parse and build it. Typed
native and canonical dynamic messages exchange map/`oneof` payloads in both
directions; canonical output includes two entries with the same map key, and
native loading keeps the second value. Reflected verification accepts those
entries, and native loading drops an appended unknown field. The same native
export also emits a `map<uint32, bytes>` whose binary values exchange with
canonical protobuf in both directions.

The custom-facet fixture exports a proto2 schema with a renamed field, a
reserved `Skip` number, and a field omitted by the facet. Canonical protobuf
builds that complete schema and reads facet output without the omitted field.
Canonical output containing all three fields passes reflected and facet-specific
typed verification; facet loading retains the selected fields and ignores the
omitted one. A `Deflt<37>` added by the rendered facet is exported under its
renamed schema field: canonical protobuf reports 37 without presence, and an
empty native facet load constructs the corresponding value.

The proto2 discarded-`oneof` fixture exports a native union with a required
field in its first alternative. Canonical protobuf and Zpb typed/reflected
verification reject malformed wire in that alternative, but accept its missing
required field when a later alternative replaces it.
The mapped-value
fixture checks that native export requires the app to supply a cross-package
`Zf.Fixed` schema. Once supplied, Zpb emits an import, canonical protobuf builds both
files, and a native nested fixed value parses canonically. The `zprotoc`
fixture exchanges generated map/`oneof` payloads with canonical protobuf in both
directions, including a `map<uint32, bytes>` with embedded zero bytes. It also
combines canonical packed `sint32` output with one
unpacked occurrence and switches to the other message-valued `oneof`
alternative. Canonical parsing, Zpb typed loading, and Zpb reflected
verification agree. The defaults fixture checks emitted numeric, string, and
byte defaults with canonical parsing, then imports canonical descriptor bytes.
It also checks that absent proto2 fields return their declared defaults without
presence, while explicitly setting those same values gives them presence and produces
wire that Zpb loads and verifies. The generated multi-`oneof` fixture
exchanges both groups with canonical dynamic protobuf in both directions.
Canonical and Zpb typed/reflected parsers also agree on interleaved wire
with an independent switch in each group. The compatibility fixture
exports old and new schemas with the same protobuf type name in separate
canonical pools. It exchanges old and new payloads in both directions, checks
default values for added fields, verifies the same wire through imported
Zpb reflection for both revisions, and confirms that re-encoding through the
old native type drops the new fields. The recursive fixture exports a recursive
native `ZuPtr` message, compiles it canonically, imports its descriptor into
Zpb reflection, and exchanges a finite two-node chain in both directions.
It also builds a parent/child payload with `Reflect::writeNested` in one
backward builder and has canonical protobuf parse both reflected values.
Typed and reflected verifiers agree on the depth boundary. The required-field
fixture exports the same native `Required` field as proto2 and proto3.
Canonical protobuf treats an empty proto2 message as
uninitialized and an empty proto3 message as initialized. Zpb reflection
follows each emitted descriptor, while typed Zpb verification still requires
the field under either schema. The presence fixture imports a canonical proto3
optional descriptor containing three optional fields and a real `oneof`.
It checks synthetic oneof order, reflection metadata, text emission and
canonical reparse, then distinguishes absent from explicitly present
zero, false, and empty string wire values.
The IP fixture exports the built-in `Zi.IP` mapping, compiles its schema
canonically, and exchanges IPv4 and IPv6 values between native and dynamic
protobuf messages. It also checks that canonical and reflected schema
verification and typed ZiIP verification accept a structurally valid
three-byte address. The fixed-value fixture exports `Zf.Fixed`, exchanges a negative mantissa
and scale in both directions with canonical protobuf, and checks that a
schema-valid scale beyond the native range passes typed verification. The
wide-value fixture compiles `Zf.Int128`, `Zf.UInt128`, and `Zf.Decimal`
schemas canonically and exchanges their high/low `uint64` words both ways,
including all-one word patterns. Both reflected and typed verification check wire structure without native
decimal range validation.
The temporal fixture exports `Zf.Time` and `Zf.DateTime`, compiles both
schemas canonically, and exchanges their field values in both directions.
It also checks that typed and reflected verification accept the native null
second sentinel.
The bitmap fixture exports `Zf.Bitmap` and `Zf.HwBitmap`, then exchanges
fixed and dynamic word bytes and a hardware bitmap range string with
canonical protobuf. It checks empty dynamic storage and structural verification independent of
native bitmap content validation.
The generated-value fixture compiles `zprotoc` output for the six canonical
numeric/temporal messages, `Zi.IP`, both bitmap messages, and an importing
`ValueRow`. Canonical protobuf parses the three source schemas and builds
their descriptors. It decodes generated Zpb output with wide integer, fixed,
IP, bitmap, hardware bitmap, all six numeric/temporal value vectors, repeated
IP, and map fields. It checks the canonical submessage fields for those
vectors, plus bitmap and hardware bitmap vectors and maps. Canonical protobuf
appends independently populated elements and map entries for IP and both bitmap
forms, and changes fixed, IP, and hardware bitmap fields; its output
passes generated Zpb verification and loading. Given the two dependency
schemas at runtime, native
reflection re-exports the generated row; canonical protobuf builds the emitted
file and resolves the external `Zi.IP` and bitmap message references.
An additional subtest exports a proto2 enum in a dependency package, imports
it in a native message, and has canonical protobuf build both emitted files.
Native and canonical messages exchange the imported enum in both directions.
The scalar vocabulary fixture exchanges signed and unsigned 8-/16-bit
integers, float and double values, text, embedded-zero bytes, and repeated
narrow integers, text, bytes, float, and double in both directions. It checks
that reflected schema export selects protobuf `float` for a native
`FloatVec<float>` and protobuf `double` for `FloatVec<double>`. It checks a
schema-valid `int32` value
outside the native `int8` range, which reflected verification accepts and typed
verification rejects. The current Clang debug run passes 26 subtests.
The C-string fixture exports native `CString` and `CStringVec` fields as
proto2 `string` fields. Canonical protobuf parses native output and supplies
new scalar and repeated values that Zpb owns as null-terminated C strings.
An explicitly present empty proto2 string passes reflected and typed verification.
The numeric wire fixture exports ordinary signed and unsigned 32/64-bit
varints, ZigZag `sint32`/`sint64`, and fixed
`fixed32`/`fixed64`/`sfixed32`/`sfixed64` scalar fields. It covers repeated
forms at both widths, including all four fixed forms. Canonical protobuf reads
native unpacked output. Its packed repeated output, followed by an unpacked
`sint32` occurrence, verifies and loads natively with the expected order.
The generated `NumericRow` fixture separately checks all twelve standard
numeric scalar forms and six repeated forms in both directions: canonical
protobuf reads generated Zpb output, and generated Zpb verifies and loads
independently populated canonical output.
The native bool-vector fixture exports `UInt8Vec` with `PB::BoolVec` as
repeated protobuf `bool`. Canonical protobuf reads native unpacked and
`PB::Packed` values; native Zpb verifies and loads canonical packed values.
The generated-message subtest also has canonical protobuf serialize and parse
a `uint32` value equal to Z's native null sentinel. The imported schema's
reflected and typed verifiers accept that schema-valid value; saving the
optional native null omits the field.
The proto2 enum subtest supplies an unrecognized numeric value. Canonical
protobuf leaves the enum field unset and records an unknown field; typed Zpb
keeps the numeric value, and its schema-aware verifier accepts the wire. This
documents the syntax-independent native enum policy and its proto2 presence
difference.
The benchmark-readiness subtest uses the small, nested, repeated/map, and
64 KiB byte/string shapes from `ZpbBench`. Native payloads parse canonically;
independently populated canonical messages verify and load natively. This is
a correctness fixture for later comparisons and runs no timing loops.
An optional timing entry point uses those same shapes:

```
./zpb/interop/zpbinterop 10000 small parse
./zpb/interop/zpbinterop 10000 nested serialize
./zpb/interop/zpbinterop 10000 repeated parse
./zpb/interop/zpbinterop 1000 large serialize
```

It exports and compiles the schema outside timing, then checks native-to-
canonical parsing, canonical-to-native verification, and semantic agreement
after a native round trip. `parse` clears and reuses a canonical dynamic
message; `serialize` reuses its output string. The CSV reports encoded input
size and a checksum, with schema work excluded. Run the TAP fixture first for
independently populated canonical input. These commands are prepared for later
matched-build comparisons; no comparative timing result is claimed here.
Unsupported descriptor semantics and the remaining feature-combination audit
are listed in the [module support matrix](../README.md#zpb-implementation-contract-and-coverage) and
[implementation audit](../../implementation.md).
