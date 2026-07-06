#!/bin/bash
set -euo pipefail

err()
{
  echo "$0: $*" >&2
  exit 1
}

[ "${MSYSTEM:-}" = "MINGW64" ] ||
  err "run this from the MSYS2 MINGW64 shell"
[ "${MSYSTEM_CARCH:-}" = "x86_64" ] ||
  err "MSYSTEM_CARCH is '${MSYSTEM_CARCH:-}', expected 'x86_64'"
[ "${MINGW_CHOST:-}" = "x86_64-w64-mingw32" ] ||
  err "MINGW_CHOST is '${MINGW_CHOST:-}', expected 'x86_64-w64-mingw32'"
[ "${MINGW_PREFIX:-}" = "/mingw64" ] ||
  err "MINGW_PREFIX is '${MINGW_PREFIX:-}', expected '/mingw64'"
case "$(uname -s)" in
MINGW64_NT-*) ;;
*) err "uname is '$(uname -s)', expected MINGW64_NT-*" ;;
esac
command -v gcc >/dev/null || err "gcc not found"
command -v dlltool >/dev/null || err "dlltool not found"
[ "$(gcc -dumpmachine)" = "x86_64-w64-mingw32" ] ||
  err "gcc target is '$(gcc -dumpmachine)', expected 'x86_64-w64-mingw32'"

SRCDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBDIR="${MINGW_PREFIX}/lib"
BINDIR="${MINGW_PREFIX}/bin"
LIBBFD="${LIBDIR}/libbfd.a"
LIBSFRAME="${LIBDIR}/libsframe.a"
LIBBFD_DEF="${SRCDIR}/mingw_libbfd.def"
LIBSFRAME_DEF="${SRCDIR}/mingw_libsframe.def"

[ -f "$LIBBFD_DEF" ] || err "missing $LIBBFD_DEF"
[ -f "$LIBSFRAME_DEF" ] || err "missing $LIBSFRAME_DEF"

if [ ! -f "$LIBBFD" ]; then
  command -v pacman >/dev/null || err "pacman not found"
  pacman -S --needed mingw-w64-x86_64-binutils
fi

[ -f "$LIBBFD" ] || err "missing $LIBBFD"
[ -f "$LIBSFRAME" ] || err "missing $LIBSFRAME"

need()
{
  local target="$1"
  shift

  [ ! -f "$target" ] && return 0

  local src
  for src in "$@"; do
    [ "$src" -nt "$target" ] && return 0
  done

  return 1
}

build_libsframe()
{
  local tmp="$1"

  dlltool --export-all-symbols -e "$tmp/libsframe.o" \
    -l "$tmp/libsframe.dll.a" -D libsframe.dll "$LIBSFRAME"
  gcc -shared -o "$BINDIR/libsframe.dll" "$tmp/libsframe.o" \
    "$LIBSFRAME" -L "$LIBDIR"
  dlltool -d "$LIBSFRAME_DEF" -l "$LIBDIR/libsframe.dll.a" \
    -D libsframe.dll
  echo "built/installed ${BINDIR}/libsframe.dll"
}

build_libbfd()
{
  local tmp="$1"

  dlltool --export-all-symbols -e "$tmp/libbfd.o" \
    -l "$tmp/libbfd.dll.a" -D libbfd.dll "$LIBBFD"
  gcc -shared -o "$BINDIR/libbfd.dll" "$tmp/libbfd.o" "$LIBBFD" \
    -L "$LIBDIR" -liberty -lintl -lz -lzstd -lsframe
  dlltool -d "$LIBBFD_DEF" -l "$LIBDIR/libbfd.dll.a" -D libbfd.dll
  echo "built/installed ${BINDIR}/libbfd.dll"
}

TMP="$(mktemp -d "${LIBDIR}/mingw_bfd_dll.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

if need "$BINDIR/libsframe.dll" "$LIBSFRAME" ||
    need "$LIBDIR/libsframe.dll.a" "$LIBSFRAME_DEF"; then
  build_libsframe "$TMP"
else
  echo "${BINDIR}/libsframe.dll is current"
fi

if need "$BINDIR/libbfd.dll" "$LIBBFD" "$LIBDIR/libsframe.dll.a" ||
    need "$LIBDIR/libbfd.dll.a" "$LIBBFD_DEF"; then
  build_libbfd "$TMP"
else
  echo "${BINDIR}/libbfd.dll is current"
fi
