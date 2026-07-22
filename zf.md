# Split `libZf` from `libZt`

## Summary

Create a new `zf` library layer between `ze` and `zi`:

`zu → zm → zt → ze → zf → zi`

Move the reflection/serialization family from `zt` into `libZf`, rename its files and public APIs from `Zt*` to `Zf*`, migrate its tests and every active dependent, and add `zf` consistently to build, include, and link paths. This is an intentional breaking API/ABI change with no compatibility headers or aliases.

## Implementation Changes

### New `zf` module and API migration

- Add `zf/Makefile.am` with `src test` subdirectories.
- Add `ZfLib.hh`/`.cc` defining `ZfAPI`, `ZfExplicit`, `ZfExtern`, and the `ZfLib` version symbol using the established `ZeLib`/`ZiLib` pattern.
- Move and rename:
  - `ZtStruct.hh/.cc` → `ZfStruct.hh/.cc`
  - `ZtJSON.hh/.cc` → `ZfJSON.hh/.cc`
  - `ZtURI.hh/.cc` → `ZfURI.hh/.cc`
  - `ZtCLI.hh/.cc` → `ZfCLI.hh/.cc`
  - `ZtCSV.hh/.cc` → `ZfCSV.hh/.cc`
  - `ZtASN1.hh` → `ZfASN1.hh`
- Rename all APIs owned by those files, including namespaces, macros, traits, ADL hooks, configuration helpers, field/type-erasure APIs, include guards, export declarations, and heap identifiers. In particular, migrate the complete `ZtStruct`/`ZtField`/`ZtVField` family to `Zf*`.
- Keep lower-layer facilities such as `ZtArray`, `ZtString`, `ZtFmt`, `ZtVFmt`, `ZtBytesFmt`, `ZtQuote`, `ZtEnum`, and `ZtRegex` in `zt`; update moved files to include and use them as `libZf` dependencies.
- Preserve the current worktree versions of the moved files, including the uncommitted CLI default-format and field-range changes.
- Remove the moved headers, sources, and tests from the `zt` manifests. Do not leave forwarding headers, aliases, or compatibility symbols.

### Build-system integration

- In `configure.ac`, add `zf` to the package-directory setup loop and generate `zf/Makefile`, `zf/src/Makefile`, and `zf/test/Makefile`, ordered after `ze` and before `zi`.
- In the top-level `Makefile.am`, insert `zf` between `ze` and `zi` in `SUBDIRS`, the QIR install directory list, and the explicit QIR build sequence.
- Define `zf/src/Makefile.am` with:
  - `AM_CPPFLAGS` ordered low-to-high: `zu`, `zm`, `zt`, `ze`, followed by `-DZF_EXPORTS`.
  - `libZf.la` containing `ZfLib.cc` and the moved compiled sources, with `ZfASN1.hh` remaining header-only.
  - `libZf_la_LIBADD` ordered high-to-low: `libZe`, `libZt`, `libZm`, `libZu`, then `@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@`, matching the current `zi` dependency model.
- Define `zf/test/Makefile.am` with include order `zu`, `zm`, `zt`, `ze`, `zf` and link order `libZf`, `libZe`, `libZt`, `libZm`, `libZu`.
- Add `zf` to `zi` source, test, and example dependencies: include paths become `... zt, ze, zf, zi ...`; link lists become `... libZi, libZf, libZe, libZt ...`.
- Apply the same ordering to every source/test/example/interop `Makefile.am` at or above `zi`, including active and optional modules: `ztcp`, `ztls`, `zquic`, `zhttp`, `zrest`, `zws`, `zfb`, `zv`, `zdb`, `zdb_pq`, `zum`, `zdf`, `zrl`, `zcmd`, `zproxy`, `zgtk`, and the currently inactive `zdash` build files.
- Do not add a `Z_ZF_LIBS` substitution: `zf` introduces no new external dependency.

### Tests, examples, and dependent code

- Move and rename the five existing component tests to `zf/test`: `ZfASN1Test`, `ZfCLITest`, `ZfCSVTest`, `ZfStructTest`, and `ZfURITest`; migrate their includes and APIs to `Zf*`. There is no standalone JSON test or `zt/example` subtree to move.
- Update all active consumers to include `Zf*.hh` and use the renamed APIs. This includes code under `zi`, `ztls`, `zquic`, `zhttp`, `zrest`, `zfb`, `zv`, `zdb`, `zdb_pq`, `zdf`, `zcmd`, `zum`, and `zgtk`, plus their tests and examples.
- Update lower-layer explanatory comments/tests that refer conceptually to the old ownership, such as `ZuStruct` documentation and the JSON-format fixture in `ZuStructTest`, without introducing an actual upward dependency.
- Update active repository documentation describing the module hierarchy or component locations. Leave archived plans and historical artifacts unchanged.

## Test Plan

- Run a tracked-source search to ensure no active code includes the six removed `Zt*.hh` headers or uses APIs owned by the moved files; separately verify that legitimate lower-layer `Zt*` types remain unchanged.
- Reconfigure the current Clang debug build with `./z.config -c -d -L /usr`, then run a clean top-level build.
- Run `make -C zf/test test`, `make -C zi/test test`, and the directly affected higher-layer tests, especially `ztls`, `zquic`, `zhttp`, `zfb`, `zv`, and `zdb`.
- Run top-level `make test`, then explicitly build configured optional consumer modules that are excluded from top-level `SUBDIRS`.
- Perform the required clean release verification with `./z.config -c /usr`, followed by top-level `make clean`, `make -j`, and `make test`.
- When the MinGW toolchain is available, build the new library and its dependents to validate `ZF_EXPORTS` and DLL import/export behavior.

## Assumptions

- The requested `Zf*` component names imply a complete public prefix migration, not merely relocating `Zt*` APIs into another shared object.
- Backward source and binary compatibility are intentionally out of scope.
- Existing unrelated worktree changes remain untouched; changes already present in files being moved are preserved in their renamed destinations.
- Include paths are always ordered from lowest to highest layer, while Z libraries in `LIBADD`/`LDADD` are ordered from highest to lowest.
