# Zfb integration tests

## zflatc round trip

`zfbsave` exports the original fixture's FBS, BFBS, normalized schema contract,
recoverable metadata manifest and payload. `zflatc` generates the declarations;
`zfbcheck` links the generated implementation and a second consumer translation
unit, checks canonical types and member heap IDs, materializes the original
payload, and writes a reverse-direction payload for `zfbsave --verify`.

The build also compiles the `.hpp`/`.cpp` variant and `--ref` variants for
`ZuObject`, `ZuPolymorph`, `ZmObject` and `ZmPolymorph`. The `ZuObject` variant
uses `--export ZfbAPI`; the `ZmPolymorph` variant also uses `--alt`.
The custom variant uses `--ref ::flatcref::Base` with its component header
supplied to every translation unit; root variants also check `--ref ::ZuObject`.
The `Wire` source-facet fixture changes root, child and inline member wire names;
`zfbcheck_wire` compares and loads that contract through generated canonical FB
metadata, and the exporter verifies the reverse payload through `Wire`.
The fixtures cover every canonical scalar/value vector mapping, bitmap and
IPv4/IPv6 values, finite mutually recursive table graphs, and repeated loading
that clears absent pointers. Union payloads cover tables, inline values,
128-bit integers, owning bytes, and NONE. Two fields using the same union prove
that byte payload heaps follow their respective containing members while the
schema contains one union declaration.
The inline fixture includes a member named `v`, exercising generated assignment
without hiding it behind the source parameter in reference-base variants.
`zfbinclude` and `zfbinclude_alt` compile separately generated dependencies in
one namespace and materialize their tables, arrays and inline union payloads.
They cover deprecated container metadata, fixed-array and deprecated aliases
matching their containing type's name, and owning
built-in roots reached through direct and indirect schema includes.
Checked-in `flatcdep.expected.*` and `flatcroot.expected.*` declarations are
compared with fresh output by the driver.
`zflatctest` drives these executables and checks CLI paths, include order,
separate dependency generation, diagnostics and preservation of old outputs.
`zfbmapcheck` builds `maps.fbs` through `zflatc` and exercises generated map
fields, nested maps, root wrappers, BFBS, and FBS. `zfbmapimport` builds a
keyed entry in one schema and consumes it from a second schema in two C++
translation units. `zfbmapexport` writes a handwritten map schema and payload;
`zfbmapgenerated` consumes that payload using generated code and writes a
changed payload for the handwritten checker. These run under `zflatctest` and
`make -C zfb/itest test`.
Runtime artifacts belong to its `ZiTestResidue` directory; build-time generated
files and exporter artifacts are covered by Automake cleanup rules.

```sh
make -C zfb/src -j4
make -C zfb/itest -j4
cd zfb/itest && prove ./zflatctest
```

`zfbroot` and its extension/reference-base variants compile generated owning
`Zf.Bytes` and `Zf.Bitmap` roots. Their schemas originate from `saveFBS` on the
library's canonical wire types. They compare semantic BFBS and field metadata,
load original payloads through `ctor()`/`alloc()`/`load()`, release source
storage, and verify reverse payloads with the original canonical codec.

Canonical `flatc` remains optional and confined to `../interop`.
