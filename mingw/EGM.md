# MinGW BFD support

## Overview

Build MSYS2 MINGW64 DLLs and import libraries for BFD and SFrame using installed static archives and explicit export definitions.

## Interface

- `mingw_bfd_dll.sh` builds `libsframe.dll`, `libbfd.dll`, and their import libraries. It locates sibling export files relative to its own path. [mingw_bfd_dll.sh:SRCDIR] [mingw_bfd_dll.sh:build_libsframe] [mingw_bfd_dll.sh:build_libbfd]
- [BFD exports](./mingw_libbfd.def) declare `LIBRARY libbfd` and explicit exports including `bfd_init`, `bfd_openr`, `bfd_close`, and internal `_bfd` symbols. [mingw_libbfd.def:EXPORTS]
- [SFrame exports](./mingw_libsframe.def) declare `LIBRARY libsframe` and decoding, encoding, and metadata functions. [mingw_libsframe.def:EXPORTS]

## Invariants

- Execution requires `MSYSTEM=MINGW64`, `MSYSTEM_CARCH=x86_64`, `MINGW_CHOST=x86_64-w64-mingw32`, `MINGW_PREFIX=/mingw64`, a `MINGW64_NT-*` kernel name, and matching GCC plus `dlltool`. [mingw_bfd_dll.sh:MSYSTEM] [mingw_bfd_dll.sh:gcc]
- Required definitions and archives must exist. `need` rebuilds absent or stale targets; BFD DLL freshness includes its SFrame import-library dependency. [mingw_bfd_dll.sh:need]

## Side Effects

- If the BFD archive is absent, the script installs `mingw-w64-x86_64-binutils` through `pacman`. [mingw_bfd_dll.sh:pacman]
- Builds write DLLs to `/mingw64/bin`, import libraries to `/mingw64/lib`, and temporary files beneath the library directory; exit cleanup removes the temporary directory. [mingw_bfd_dll.sh:build_libsframe] [mingw_bfd_dll.sh:build_libbfd] [mingw_bfd_dll.sh:TMP]

## Verification

From the repository root, run `bash -n mingw/mingw_bfd_dll.sh` for syntax checking. Functional verification requires the specified MSYS2 environment and performs the installation effects above. No commands were run for this documentation change.
