# MinGW support files

MSYS2/Mingw64 ships `libbfd.a` and related headers through the
`mingw-w64-x86_64-binutils` package, but it does not provide `libbfd.dll`.
Z uses BFD for backtracing, so `mingw_bfd_dll.sh` builds local DLLs and import
libraries from the static MinGW archives:

- `/mingw64/bin/libsframe.dll`
- `/mingw64/lib/libsframe.dll.a`
- `/mingw64/bin/libbfd.dll`
- `/mingw64/lib/libbfd.dll.a`

The script must be run from the MSYS2 `MINGW64` shell. It asserts the x86_64
Mingw64 environment before doing any work, installs
`mingw-w64-x86_64-binutils` if `libbfd.a` is missing, and uses the `.def` files
from this directory regardless of the current working directory.

Usage:

```sh
/path/to/z/mingw/mingw_bfd_dll.sh
```

From the repository root:

```sh
./mingw/mingw_bfd_dll.sh
```

The script is idempotent. Re-running it leaves current DLLs/import libraries in
place and rebuilds only when an output is missing or older than its static
archive or `.def` input.
