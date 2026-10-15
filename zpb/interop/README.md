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

The current program passes 19 TAP subtests. The remaining Phase 6 interop
cases are pending.
