m4_define([Z_VERSION_], m4_esyscmd([cat VERSION | tr -d '\n']))
AC_INIT([zlib],[Z_VERSION_])
Z_VERSION=Z_VERSION_

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS(config.h)

AM_INIT_AUTOMAKE([subdir-objects -Wno-portability])

AC_LANG([C++])
AC_PROG_CXX
AC_PROG_CC

AC_PATH_PROG([GO], [go])
AM_CONDITIONAL([HAVE_GO], [test -n "$GO"])

# Autoconf may append a standard-mode probe to the compiler command.
# This project manages language modes explicitly.
for z_var in CC CXX; do
  eval "z_compiler=\$$z_var"
  z_clean_compiler=
  for z_arg in $z_compiler; do
    AS_CASE([$z_arg],
      [-std=*], [],
      [-std:*], [],
      [z_clean_compiler="${z_clean_compiler}${z_clean_compiler:+ }$z_arg"])
  done
  eval "$z_var=\$z_clean_compiler"
done

LT_INIT([win32-dll shared disable-static])
LT_LANG([C++])

Z_VMAJOR=`echo $Z_VERSION | cut -d. -f1`
Z_VMINOR=`echo $Z_VERSION | cut -d. -f2`
Z_VPATCH=`echo $Z_VERSION | cut -d. -f3`

CPPFLAGS="-DZ_VMAJOR=$Z_VMAJOR -DZ_VMINOR=$Z_VMINOR -DZ_VPATCH=$Z_VPATCH $CPPFLAGS"

AX_C_VA_LIST_IS_ARRAY

# ensure perl is available
AC_CHECK_PROG(PERL, "perl", "yes", "no")
AS_IF([test ".$PERL" = ".no"], [
  AC_MSG_ERROR([perl required])
])

# compilers
AM_CONDITIONAL([MINGW], [test ".$mingw" = ".yes"])
AM_CONDITIONAL([GXX], [test ".$GXX" = ".yes"])

# concurrency kit
PKG_CHECK_MODULES([CK], [ck >= 0.6], [], [dnl
  AC_MSG_ERROR([libck required])
])
AC_SUBST(CK_LIBS)
CPPFLAGS="$CK_CFLAGS $CPPFLAGS"

# hwloc
PKG_CHECK_MODULES([HWLOC], [hwloc >= 2], [], [dnl
  AC_MSG_ERROR([libhwloc required])
])
AC_SUBST(HWLOC_LIBS)
CPPFLAGS="$HWLOC_CFLAGS $CPPFLAGS"

# pcre
AX_PATH_LIB_PCRE(, [AC_MSG_ERROR([libpcre required])])
CPPFLAGS="$PCRE_CPPFLAGS $CPPFLAGS"
LDFLAGS="$PCRE_LDFLAGS $LDFLAGS"

# system MT libraries
AC_MSG_CHECKING([system multithreading libraries])
Z_CPPFLAGS="-D_REENTRANT -D_GNU_SOURCE"
case $build_os in
  linux-*) 
    Z_CFLAGS=""
    Z_CXXFLAGS=""
    Z_LDFLAGS="-rdynamic"
    Z_SO_LDFLAGS="-release $Z_VERSION"
    Z_LIBS="-lm"
    Z_MT_LIBS="$CK_LIBS $HWLOC_LIBS -lpthread -lrt -latomic"
    Z_ZT_LIBS="$PCRE_LIBS"
    Z_IO_LIBS="-ldl"
    Z_TERM_LIBS="-lncurses"
    ;;
  mingw*)
    mingw=yes
    case $build_cpu in
      i686) mingw32=yes;;
      x86_64) mingw64=yes;;
      *) ;;
    esac
    Z_CPPFLAGS="-DUNICODE $Z_CPPFLAGS"
    Z_CFLAGS="-mstackrealign -fuse-ld=lld"
    Z_CXXFLAGS="-mstackrealign -fuse-ld=lld"
    Z_LDFLAGS="-Wl,--enable-auto-import"
    Z_SO_LDFLAGS="-no-undefined"
    Z_LIBS="-lm"
    Z_MT_LIBS="$CK_LIBS $HWLOC_LIBS -latomic"
    Z_ZT_LIBS="$PCRE_LIBS -liconv"
    Z_IO_LIBS="-lws2_32 -lwsock32 -ldnsapi"
    Z_TERM_LIBS=""
    ;;
  *)
    Z_CFLAGS=""
    Z_CXXFLAGS=""
    Z_LDFLAGS=""
    Z_SO_LDFLAGS=""
    Z_LIBS="-lm"
    Z_MT_LIBS="$CK_LIBS $HWLOC_LIBS -lpthread"
    Z_ZT_LIBS="$PCRE_LIBS"
    Z_IO_LIBS="-ldl"
    Z_TERM_LIBS="-lterminfo"
    ;;
esac

AC_MSG_RESULT([${Z_MT_LIBS}])

# c-ares
PKG_CHECK_MODULES([CARES], [libcares], [], [dnl
  AC_MSG_ERROR([libcares required])
])
AC_SUBST(CARES_LIBS)
AC_SUBST(CARES_CFLAGS)
CPPFLAGS="$CARES_CFLAGS $CPPFLAGS"
Z_IO_LIBS="$Z_IO_LIBS $CARES_LIBS"

Z_OFLAGS="-fno-math-errno -fno-trapping-math -fno-rounding-math -fno-signaling-nans"
Z_CFLAGS="$Z_CFLAGS $Z_OFLAGS"
Z_CXXFLAGS="$Z_CXXFLAGS $Z_OFLAGS"

AS_CASE([$CC],
  [*clang*], [
    Z_WFLAGS="-Wall -Wno-parentheses -Wno-invalid-offsetof -Wno-misleading-indentation -Wno-sign-compare -Wno-empty-body -Wno-unknown-warning-option -Wno-ignored-optimization-argument -Wno-int-in-bool-context -Wno-varargs -Wno-unneeded-internal-declaration -Wno-unused-function" # C and C++
    Z_CFLAGS="$Z_CFLAGS $Z_WFLAGS"
    Z_CXXFLAGS="-std=gnu++2b -fno-aligned-allocation -ftemplate-backtrace-limit=0 -fconstexpr-steps=10000000 -Wno-unused-private-field -Wno-deprecated-enum-enum-conversion -Wno-deprecated-enum-compare -Wno-deprecated-anon-enum-enum-conversion -Wno-defaulted-function-deleted -Wno-unused-function $Z_CXXFLAGS $Z_WFLAGS"
  ],
  [*gcc*], [
    Z_WFLAGS="-Wall -Wno-parentheses -Wno-invalid-offsetof -Wno-misleading-indentation -Wno-sign-compare -Wno-class-memaccess -Wno-unused-function" # C and C++
    Z_CFLAGS="$Z_CFLAGS $Z_WFLAGS"
    Z_CXXFLAGS="-std=gnu++2b -fstrict-aliasing -ftemplate-backtrace-limit=0 -Wno-non-template-friend -Wno-enum-compare -Wno-deprecated-enum-enum-conversion -Wno-int-in-bool-context -Wno-unused-value -Wno-sfinae-incomplete $Z_CXXFLAGS $Z_WFLAGS"
  ])

AC_SUBST(Z_CPPFLAGS)
AC_SUBST(Z_CFLAGS)
AC_SUBST(Z_CXXFLAGS)
AC_SUBST(Z_LDFLAGS)
AC_SUBST(Z_SO_LDFLAGS)
AC_SUBST(Z_LIBS)
AC_SUBST(Z_MT_LIBS)
AC_SUBST(Z_ZT_LIBS)
AC_SUBST(Z_IO_LIBS)
AC_SUBST(Z_TERM_LIBS)

# zpicotls
PKG_CHECK_MODULES([PTLS], [
  zpicotls-core >= 1.3
  zpicotls-openssl >= 1.3
  zpicotls-fusion >= 1.3
], [], [dnl
  AC_MSG_ERROR([zpicotls >= 1.3 with OpenSSL and fusion support required])
])
AC_SUBST(PTLS_LIBS)
AC_SUBST(PTLS_CFLAGS)

# flatbuffers
PKG_CHECK_MODULES([FBS], [flatbuffers >= 24], [], [dnl
  AC_MSG_ERROR([libflatbuffers required])
])
AC_SUBST(FBS_LIBS)
CPPFLAGS="$FBS_CFLAGS $CPPFLAGS"

# netlink (non-MinGW only) (optional)
AS_IF([test ".$mingw" != ".yes"], [
  AC_ARG_WITH([netlink], AS_HELP_STRING([--with-netlink], [Build with NetLink]))
])
AM_CONDITIONAL([NETLINK], [test ".$with_netlink" = ".yes"])

# postgres libpq (optional)
AC_ARG_WITH([pq], AS_HELP_STRING([--with-pq], [Build with PostgreSQL]))
AS_IF([test ".$with_pq" = ".yes"], [
  PKG_CHECK_MODULES([PQ], [libpq >= 16], [], [dnl
    AC_MSG_ERROR([libpq unavailable])
  ])
  AC_SUBST(PQ_CFLAGS)
  AC_SUBST(PQ_LIBS)
])
AM_CONDITIONAL([LIBPQ], [test ".$with_pq" = ".yes"])

# Gtk (optional)
AC_ARG_WITH([gtk], AS_HELP_STRING([--with-gtk], [Build with Gtk]))
AS_IF([test ".$with_gtk" = ".yes"], [
  PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= 3.24.0], [], [dnl
    AC_MSG_NOTICE([Gtk unavailable])
  ])
  # cleanup and de-duplicate Gtk CFLAGS, LIBS
  GTK_CFLAGS=`echo $GTK_CFLAGS | perl -pe 's/-pthread\s+//g; s/-mms-bitfields//g; s|/@<:@^/@:>@+/\.\./|/|g; s/-I(@<:@^ \n@:>@+)(@<:@ \n@:>@)/$I{$1}++ ? "" : "-I${1}${2}"/ge'`
  GTK_LIBS=`echo $GTK_LIBS | perl -pe 's/-pthread\s+//g; s|/@<:@^/@:>@+/\.\./|/|g; s/-L(@<:@^ \n@:>@+)(@<:@ \n@:>@)/$L{$1}++ ? "" : "-L${1}${2}"/ge; s/-l(@<:@^ \n@:>@+)(@<:@ \n@:>@)/$l{$1}++ ? "" : "-l${1}${2}"/ge; s/(-Wl,-R,|-R)(@<:@^ \n@:>@+)(@<:@ \n@:>@)/$R{$2}++ ? "" : "${1}${2}${3}"/ge;'`
  AC_SUBST(GTK_CFLAGS)
  AC_SUBST(GTK_LIBS)
])
AM_CONDITIONAL([GTK], [test ".$with_gtk" = ".yes"])

# cleanup and de-duplicate CPPFLAGS and LDFLAGS
CPPFLAGS=`echo $CPPFLAGS | perl -pe 's/-I(@<:@^ \n@:>@+)(@<:@ \n@:>@)/$I{$1}++ ? "" : "-I${1}${2}"/ge;'`
LDFLAGS=`echo $LDFLAGS | perl -pe 's|/[^/]+/\.\./|/|g; s/-L(@<:@^ \n@:>@+)(@<:@ \n@:>@)/$L{$1}++ ? "" : "-L${1}${2}"/ge; s/(-Wl,-R,|-R)(@<:@^ \n@:>@+)(@<:@ \n@:>@)/$R{$2}++ ? "" : "${1}${2}${3}"/ge'`

for i in zu zm zt ze zf zi ztcp ztls zquic zhttp zrest zws zfb zv zdb zdb_pq zum zdf zrl zcmd zproxy zgtk; do {
  for j in src ext; do {
    if test -d $i/$j -a ! -e $i/$j/AC_PACKAGE_NAME; then (
      cd $i/$j;
      if test ".$mingw" != ".yes"; then
	ln -s . AC_PACKAGE_NAME
      else
	env MSYS2_ARG_CONV_EXCL="*" cmd /c mklink /d AC_PACKAGE_NAME .
      fi
    ); fi
  }; done
}; done

AC_CONFIG_FILES([Makefile \
	zu/Makefile zu/src/Makefile zu/test/Makefile zu/bench/Makefile \
	zm/Makefile zm/src/Makefile zm/test/Makefile zm/bench/Makefile \
	zt/Makefile zt/src/Makefile zt/test/Makefile \
	ze/Makefile ze/src/Makefile ze/test/Makefile \
	zf/Makefile zf/src/Makefile zf/test/Makefile zf/interop/Makefile \
	zi/Makefile zi/src/Makefile zi/test/Makefile zi/example/Makefile \
	ztcp/Makefile ztcp/src/Makefile ztcp/test/Makefile ztcp/example/Makefile \
	ztls/Makefile ztls/src/Makefile ztls/test/Makefile ztls/example/Makefile \
	zquic/Makefile zquic/src/Makefile zquic/test/Makefile \
	zquic/example/Makefile \
	zhttp/Makefile zhttp/src/Makefile zhttp/test/Makefile \
	zhttp/util/Makefile zhttp/itest/Makefile zhttp/interop/Makefile \
	zrest/Makefile zrest/src/Makefile zrest/test/Makefile \
	zrest/itest/Makefile zrest/example/Makefile zrest/interop/Makefile \
	zws/Makefile zws/src/Makefile zws/test/Makefile \
	zws/bench/Makefile zws/interop/Makefile \
	zfb/Makefile zfb/src/Makefile zfb/test/Makefile \
	zv/Makefile zv/src/Makefile zv/test/Makefile \
	zdb/Makefile zdb/src/Makefile zdb/test/Makefile zdb/itest/Makefile \
	zdb_pq/Makefile zdb_pq/src/Makefile zdb_pq/test/Makefile \
	zdb_pq/itest/Makefile \
	zum/Makefile zum/src/Makefile zum/test/Makefile \
	zdf/Makefile zdf/src/Makefile zdf/test/Makefile zdf/itest/Makefile \
	zrl/Makefile zrl/src/Makefile zrl/test/Makefile zrl/example/Makefile \
	zcmd/Makefile zcmd/src/Makefile zcmd/test/Makefile zcmd/itest/Makefile \
	zproxy/Makefile zproxy/src/Makefile zproxy/test/Makefile \
	zgtk/Makefile zgtk/src/Makefile zgtk/test/Makefile \
	zgtk/example/Makefile zdash/Makefile zdash/src/Makefile \
	zdash/test/Makefile])
AC_OUTPUT
