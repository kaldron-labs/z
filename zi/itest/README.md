# Test residue helpers

C++ tests use `ZiTestResidue` from `zi/test`. Python, shell, and Go tests use
the helpers in this directory. Set `ZI_LOGDIR` to choose the artifact root;
otherwise it is the current directory. Each test creates an owned directory,
removes it on success, and prints and retains it on failure. At most eight
completed failed directories per test name are retained. Shared-memory names
registered with a helper are unlinked on either outcome.

Python drivers add `zi/itest` to `PYTHONPATH`, then use
`with Residue("test-name") as residue:` from `zi_test_residue`. Create files
under `residue.directory` or use `residue.path("name")`. Call
`residue.success()` only after all checks pass and all children stop. Use
`residue.tmp_dir("tag")` only when the tested API requires `/tmp`. Use
`residue.tmp_file("tag")` when a test must deliberately place a file at a
system-temp path; it is retained as `*.failed` after failure.

Shell drivers source `zi-test-residue.sh`, call `zi_residue_init test-name`,
write beneath `$ZI_RESIDUE_DIR`, and install an EXIT trap that saves `$?`,
stops children, and calls `zi_residue_finish "$status"`.
Call `zi_residue_shm "$name"` before starting any process that creates a
named ring.

Go tests import the local `zlib/zi/itest/residue` module, use
`residue.New(t, "test-name")` before registering child-process cleanup, and
write beneath `r.Directory` or `r.Path("name")`. The helper's `t.Cleanup`
observes `t.Failed()` after later cleanup callbacks have run. Consumer Go
modules use a local `replace` directive to point at this directory.

The QIR runner `scripts/qir-run-local` publishes results below
`zhttp/interop/qir-results` by default, or at its caller's `--out` path.
Those are explicit results rather than test residue and are retained until
the caller removes them. Tests do not delete a caller-provided database path,
QIR output path, or runner-owned `/logs` mount.

## Heap tuning

Build the configured module tree using the job counts in
[heaptune.md](../../heaptune.md), then run `./zi/itest/zheaptune` or
`prove -v ./zi/itest/zheaptune`. It also runs from `make -C zi/itest test`
and `make -C zi test`. The `zheapwork` fixture is built alongside the TAP
driver and is launched by it; it is not a standalone TAP driver.
Set `Z_HEAPTUNE_CASE` to a case name (for example `cold`, `selective`, or
`cpuset`) to run that case alone. Unknown names fail the driver.
Use separate `ZI_LOGDIR` roots when running multiple copies concurrently;
residue paths use the test name.

Each case runs the fixture in fresh processes: call `ZiHeapTune::load()` first
in `main`, run deterministic bursts, overwrite `Z_HEAPTUNE` with
`ZiHeapTune::save()`, and replay the resulting configuration. Explicit
headroom uses `save(headroom)`; open/write failures fail the fixture. The late
case uses `ZiHeapTune::init` for its separate input configuration. The driver
checks initial and drained telemetry, allocation counts, fallback peaks,
independent partition/class capacities, sizing arithmetic, unused arenas,
and file replacement. Size-boundary cases verify the full payload.

The fixture syntax is
`zheapwork scenario count repeat report [headroom|-] [late-config]`.
Scenarios are `fixed`, `multi`, `unusedpair`, `variable`, `shift1`,
`partitions`, `sequential`, `keys`, `late`, `shared`, `unused`, and
`boundaries`. Omitting headroom or using `-` calls `ZiHeapTune::save()` with
its default headroom.
Set `Z_HEAPTUNE` to an owned configuration file when invoking the fixture
manually: it overwrites that file. The driver supplies a child-only path
and leaves the caller's environment and configuration file alone.

The default child timeout is 30 seconds. `Z_HEAPTUNE_TIMEOUT` overrides it
(1 through 3600 seconds); it bounds execution without imposing a performance
threshold. On failure, the owned `ZiTestResidue` directory retains each
generation's input/output CSV, structured snapshots, `heapCSV()` output,
and child TAP log. Successful runs remove the directory.

Use Valgrind only when investigating observed memory corruption or crashes.
To instrument both the parent and children, for example:

```sh
Z_HEAPTUNE_TIMEOUT=120 \
Z_HEAPTUNE_CHILD='libtool exec valgrind --leak-check=full --error-exitcode=99' \
  libtool exec valgrind --leak-check=full --error-exitcode=99 ./zi/itest/zheaptune
```

Use the configured build's `libtool` by absolute path if it is not on `PATH`.
Never launch `.libs` binaries directly.
## Hash tuning

The `zhashtune` TAP driver launches `zhashwork` in fresh processes, starting
with an empty or seeded `Z_HASHTUNE` configuration. The fixture loads through
`ZiHashTune::load()` first in `main` and saves through `ZiHashTune::save()`
while its workload tables remain registered. Only `bits` changes: a resized
run exports its final bits; a run without resizing uses the retained peak
and headroom, allowing shrinkage below the default.

Once rebuilding is resumed, build with Clang/debug, `-j8` through `ztc` and
`-j4` thereafter, then run `./zi/itest/zhashtune` or
`prove -v ./zi/itest/zhashtune`. It is also included in `make -C zi/itest test`
and `make -C zi test`. Set `Z_HASHTUNE_CASE` to a named case such as `grown`,
`below-default`, `same-id-overlap`, `linear-load`, or `utility` for a focused
run. Unknown case names fail.

The fixture syntax is
`zhashwork scenario count repeat report [headroom|-] [late-config]`.
The driver supplies child-only configuration paths. It retains every input
and output generation, structured snapshot, diagnostic hash CSV, and child
TAP log on failure through `ZiTestResidue`, and removes owned residue on
success. Use separate `ZI_LOGDIR` roots for concurrent driver runs.

`Z_HASHTUNE_TIMEOUT` sets the child timeout (default 30 seconds, valid range
1–3600). `Z_HASHTUNE_CHILD` supplies an optional instrumentation command
prefix. The launcher kills and reaps timed-out children using the established
heap harness process-group/job conventions. Use Valgrind only if needed to
investigate observed corruption or crashes, through `libtool exec` and normal
wrapper paths. Do not launch `.libs` binaries directly.

## zflatc round trip

`zifbsave` exports the original fixture's FBS, BFBS, normalized schema contract,
recoverable metadata manifest and payload. `zflatc` generates the declarations;
`zifbcheck` links the generated implementation and a second consumer translation
unit, checks canonical types and member heap IDs, materializes the original
payload, and writes a reverse-direction payload for `zifbsave --verify`.

The build also compiles the `.hpp`/`.cpp` variant and `--ref` variants for
`ZuObject`, `ZuPolymorph`, `ZmObject` and `ZmPolymorph`. The `ZuObject` variant
uses `--export ZiAPI`; the `ZmPolymorph` variant also uses `--alt`.
The custom variant uses `--ref ::flatcref::Base` with its component header
supplied to every translation unit; root variants also check `--ref ::ZuObject`.
The `Wire` source-facet fixture changes root, child and inline member wire names;
`zifbcheck_wire` compares and loads that contract through generated canonical FB
metadata, and the exporter verifies the reverse payload through `Wire`.
The fixtures cover every canonical scalar/value vector mapping, bitmap and
IPv4/IPv6 values, finite mutually recursive table graphs, and repeated loading
that clears absent pointers. Union payloads cover tables, inline values,
128-bit integers, owning bytes, and NONE. Two fields using the same union prove
that byte payload heaps follow their respective containing members while the
schema contains one union declaration.
The inline fixture includes a member named `v`, exercising generated assignment
without hiding it behind the source parameter in reference-base variants.
`zifbinclude` and `zifbinclude_alt` compile separately generated dependencies in
one namespace and materialize their tables, arrays and inline union payloads.
They cover deprecated container metadata, fixed-array and deprecated aliases
matching their containing type's name, and owning
built-in roots reached through direct and indirect schema includes.
Checked-in `flatcdep.expected.*` and `flatcroot.expected.*` declarations are
compared with fresh output by the driver.
`zflatctest` drives these executables and checks CLI paths, include order,
separate dependency generation, diagnostics and preservation of old outputs.
`zifbmapcheck` builds `maps.fbs` through `zflatc` and exercises generated map
fields, nested maps, root wrappers, BFBS, and FBS. `zifbmapimport` builds a
keyed entry in one schema and consumes it from a second schema in two C++
translation units. `zifbmapexport` writes a handwritten map schema and payload;
`zifbmapgenerated` consumes that payload using generated code and writes a
changed payload for the handwritten checker. These run under `zflatctest` and
`make -C zi/itest test`.
Runtime artifacts belong to its `ZiTestResidue` directory; build-time generated
files and exporter artifacts are covered by Automake cleanup rules.

```sh
make -C zi/src -j4
make -C zi/itest -j4
cd zi/itest && prove ./zflatctest
```

`zifbroot` and its extension/reference-base variants compile generated owning
`Zf.Bytes` and `Zf.Bitmap` roots. Their schemas originate from `saveFBS` on the
library's canonical wire types. They compare semantic BFBS and field metadata,
load original payloads through `ctor()`/`alloc()`/`load()`, release source
storage, and verify reverse payloads with the original canonical codec.

Canonical `flatc` remains optional and confined to `../interop`.

## zprotoc integration

`zprotocconsumer` compiles the generated `zprotoc_simple.proto` message in a
separate translation unit and exchanges protobuf bytes with a handwritten
`ZiPB` type, including a finite `Row` → `Extra` → `Row` reference chain and
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
ZiPB targets with the configured Clang debug build:

```sh
make -C zi/src -j4 zprotoc
make -C zi/itest -j4 zprotoctest zprotocimport
./zi/itest/zprotoctest
./zi/itest/zprotocimport
```

`zprotocvalues` compiles the six numeric/temporal mapped-value schemas, the
canonical address and bitmap schemas, and an importing `ValueRow`. It checks
singular, repeated, and map values, plus payload exchange with handwritten
mappings. Invalid canonical value shapes fail in `zprotoctest` before output
replacement.

```sh
make -C zi/itest -j4 zprotocvalues
./zi/itest/zprotocvalues
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
