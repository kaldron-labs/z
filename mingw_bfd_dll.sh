#!/bin/bash
LIBDIR="${MINGW_PREFIX}/lib"
BINDIR="${MINGW_PREFIX}/bin"
LIBBFD="${LIBDIR}/libbfd.a"
[ ! -f "$LIBBFD" ] && pacman -S mingw-w64-x86_64-binutils
cp mingw_libbfd.def "${LIBDIR}/libbfd.def"
cp mingw_libsframe.def "${LIBDIR}/libsframe.def"
cd "$LIBDIR"
dlltool --export-all-symbols -e libsframe.o -l libsframe.dll.a -D libsframe.dll libsframe.a
gcc -shared -o ${BINDIR}/libsframe.dll libsframe.o libsframe.a -L .
rm libsframe.o
dlltool -d libsframe.def -l libsframe.dll.a -D libsframe.dll
echo "built/installed ${BINDIR}/libsframe.dll"
dlltool --export-all-symbols -e libbfd.o -l libbfd.dll.a -D libbfd.dll libbfd.a
gcc -shared -o ${BINDIR}/libbfd.dll libbfd.o libbfd.a -L . -liberty -lintl -lz -lzstd -lsframe
rm libbfd.o
dlltool -d libbfd.def -l libbfd.dll.a -D libbfd.dll
echo "built/installed ${BINDIR}/libbfd.dll"
