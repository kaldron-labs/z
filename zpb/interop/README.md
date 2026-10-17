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
