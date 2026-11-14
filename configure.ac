m4_define([Z_VERSION_], m4_esyscmd([cat VERSION | tr -d '\n']))
AC_INIT([zlib],[Z_VERSION_])
Z_VERSION=Z_VERSION_

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS(config.h)

AM_INIT_AUTOMAKE([foreign subdir-objects -Wno-portability])

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

AS_CASE([$host_os], [linux*], [z_linux=yes], [z_linux=no])
AM_CONDITIONAL([LINUX], [test "$z_linux" = yes])
AS_IF([test "$z_linux" = yes], [
  AC_MSG_CHECKING([for Zxdp AF_XDP and BPF kernel headers])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <linux/rtnetlink.h>
  ]], [[
    sockaddr_xdp address{};
    address.sxdp_family = AF_XDP;
    address.sxdp_flags = XDP_USE_NEED_WAKEUP;
    xdp_umem_reg registration{};
    registration.chunk_size = 4096;
    registration.headroom = XDP_PACKET_HEADROOM;
    xdp_mmap_offsets offsets{};
    offsets.rx.flags = offsets.tx.flags = offsets.fr.flags = offsets.cr.flags = 0;
    xdp_desc descriptor{};
    descriptor.options = 0;
    xdp_options options{};
    options.flags = XDP_OPTIONS_ZEROCOPY;
    bpf_attr attribute{};
    attribute.map_type = BPF_MAP_TYPE_XSKMAP;
    attribute.prog_type = BPF_PROG_TYPE_XDP;
    attribute.expected_attach_type = BPF_XDP;
    (void)address; (void)registration; (void)offsets;
    (void)descriptor; (void)options; (void)attribute;
    (void)__NR_bpf; (void)SO_NETNS_COOKIE; (void)SOL_XDP;
    (void)XDP_MMAP_OFFSETS; (void)XDP_UMEM_REG; (void)XDP_OPTIONS;
    (void)XDP_RX_RING; (void)XDP_TX_RING; (void)XDP_UMEM_FILL_RING;
    (void)XDP_UMEM_COMPLETION_RING; (void)XDP_RING_NEED_WAKEUP;
    (void)XDP_PGOFF_RX_RING; (void)XDP_PGOFF_TX_RING;
    (void)XDP_UMEM_PGOFF_FILL_RING; (void)XDP_UMEM_PGOFF_COMPLETION_RING;
    (void)IFLA_XDP_EXPECTED_FD; (void)IFLA_XDP_DRV_PROG_ID;
    (void)IFLA_XDP_SKB_PROG_ID; (void)IFLA_XDP_HW_PROG_ID;
    (void)XDP_FLAGS_UPDATE_IF_NOEXIST; (void)XDP_FLAGS_REPLACE;
    (void)BPF_OBJ_GET_INFO_BY_FD; (void)BPF_MAP_CREATE; (void)BPF_PROG_LOAD;
  ]])], [AC_MSG_RESULT([yes])], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Linux headers with AF_XDP ring flags/options, BPF XSK maps, expected-FD XDP attachment and SO_NETNS_COOKIE are required for Zxdp])
  ])
])
AS_IF([test "$z_linux" = yes], [
  PKG_CHECK_MODULES([DBUS], [dbus-1], [z_have_dbus=yes], [z_have_dbus=no])
], [z_have_dbus=no])
AM_CONDITIONAL([HAVE_DBUS], [test "$z_have_dbus" = yes])

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
    Z_CXXFLAGS="-fvisibility=hidden"
    # Compress final ELF DWARF while reusing uncompressed input objects.
    Z_LDFLAGS="-rdynamic -Wl,--compress-debug-sections=zstd"
    Z_SO_LDFLAGS="-release $Z_VERSION -Wl,--compress-debug-sections=zstd"
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
    Z_MT_LIBS="$CK_LIBS $HWLOC_LIBS -latomic -lmincore"
    Z_ZT_LIBS="$PCRE_LIBS -liconv"
    Z_IO_LIBS="-lws2_32 -lwsock32 -ldnsapi -ladvapi32"
    Z_TERM_LIBS=""
    ;;
  *)
    Z_CFLAGS=""
    Z_CXXFLAGS="-fvisibility=hidden"
    Z_LDFLAGS=""
    Z_SO_LDFLAGS=""
    Z_LIBS="-lm"
    Z_MT_LIBS="$CK_LIBS $HWLOC_LIBS -lpthread"
    Z_ZT_LIBS="$PCRE_LIBS"
    Z_IO_LIBS="-ldl"
    Z_TERM_LIBS="-lterminfo"
    ;;
esac

# ZmDemangle's inline implementation requires these in consumer links too.
Z_MT_LIBS="$Z_MT_LIBS -lbfd -liberty"

AC_MSG_RESULT([${Z_MT_LIBS}])

# compilers
AM_CONDITIONAL([MINGW], [test ".$mingw" = ".yes"])
AM_CONDITIONAL([GXX], [test ".$GXX" = ".yes"])

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
PKG_CHECK_VAR([PTLS_LIBDIR], [zpicotls-core], [libdir], [], [
  AC_MSG_ERROR([zpicotls-core pkg-config libdir required])
])
# Ztls public template headers need the selected dependency's include path.
CPPFLAGS="$PTLS_CFLAGS $CPPFLAGS"
z_save_LIBS=$LIBS
LIBS="$PTLS_LIBS $LIBS"
AC_MSG_CHECKING([for zpicotls serial record output])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <zpicotls.h>
#if !PTLS_HAVE_RECORD_CHUNKS
#error serial record output required
#endif]], [[
  int n = ptls_tx_send_chunks(0, 0, 0, 0);
  n += ptls_handshake_chunks(0, 0, 0, 0, 0);
  return n + ptls_recv_chunks(0, 0, 0, 0, 0);
]])], [AC_MSG_RESULT([yes])], [
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([zpicotls with PTLS_HAVE_RECORD_CHUNKS and serial record APIs required; rebuild the zpicotls dependency])
])
AC_MSG_CHECKING([for zpicotls incremental record receive])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <zpicotls.h>
#if !PTLS_HAVE_RECEIVE_STREAM
#error incremental record receive required
#endif]], [[
  ptls_plaintext_sink_t plaintext = {0};
  size_t length = 0;
  int n = ptls_receive_stream_check(0);
  return n + ptls_receive_stream(0, &plaintext, 0, 0, &length, 0);
]])], [AC_MSG_RESULT([yes])], [
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([zpicotls with PTLS_HAVE_RECEIVE_STREAM and incremental receive APIs required; rebuild the zpicotls dependency])
])
LIBS=$z_save_LIBS

PKG_CHECK_MODULES([CRYPTO], [libcrypto >= 3.5], [], [
  AC_MSG_ERROR([OpenSSL libcrypto >= 3.5 with ML-KEM-768 support required])
])
AC_SUBST(CRYPTO_LIBS)
AC_SUBST(CRYPTO_CFLAGS)
Z_CPPFLAGS="$Z_CPPFLAGS $CRYPTO_CFLAGS"
z_save_CPPFLAGS=$CPPFLAGS
z_save_LIBS=$LIBS
CPPFLAGS="$CRYPTO_CFLAGS $CPPFLAGS"
LIBS="$CRYPTO_LIBS $LIBS"
AC_RUN_IFELSE([AC_LANG_PROGRAM([[#include <openssl/evp.h>
#include <openssl/core_names.h>]], [[
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(0, "ML-KEM-768", 0);
  if (!ctx) return 1;
  EVP_PKEY *key = 0;
  int ok = EVP_PKEY_keygen_init(ctx) == 1 &&
    EVP_PKEY_keygen(ctx, &key) == 1 && key;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) return 1;
  ctx = EVP_PKEY_CTX_new_from_pkey(0, key, 0);
  ok = ctx && EVP_PKEY_encapsulate_init(ctx, 0) == 1 &&
    OSSL_PKEY_PARAM_ML_KEM_SEED[0] != 0;
  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(key);
  return !ok;
]])], [], [AC_MSG_ERROR([OpenSSL ML-KEM-768 provider/API unavailable])],
  [AC_MSG_WARN([cross-compiling: ML-KEM-768 provider must be verified at run time])])
CPPFLAGS=$z_save_CPPFLAGS
LIBS=$z_save_LIBS

# Canonical FlatBuffers is optional and used only by explicit interop targets.
# Do not add its flags to the production build or require it for configure.
PKG_CHECK_MODULES([FB_INTEROP], [flatbuffers >= 24], [], [
  FB_INTEROP_CFLAGS=
  FB_INTEROP_LIBS=
])

# Canonical protobuf is used only by the explicit Zpb interop target.
PKG_CHECK_MODULES([PB_INTEROP], [protobuf >= 25], [z_have_pb_interop=yes], [
  z_have_pb_interop=no
  PB_INTEROP_CFLAGS=
  PB_INTEROP_LIBS=
])
AM_CONDITIONAL([HAVE_PB_INTEROP], [test "x$z_have_pb_interop" = "xyes"])

# Independent QuickFIX/C++ fixture dependency; never add flags to Zfix libraries.
AC_ARG_WITH([quickfix],
  AS_HELP_STRING([--with-quickfix@<:@=PREFIX@:>@], [Build independent FIX interop fixtures (default: no)]),
  [], [with_quickfix=no])
AC_ARG_VAR([QUICKFIX_DICT], [Canonical FIX44.xml for independent QuickFIX fixtures])
z_have_quickfix=no
QUICKFIX_CFLAGS=
QUICKFIX_LIBS=
AS_IF([test "x$with_quickfix" != "xno"], [
  AS_IF([test "x$with_quickfix" = "xyes"], [
    PKG_CHECK_MODULES([QUICKFIX], [quickfix >= 1.16.0], [], [
      AC_MSG_ERROR([QuickFIX >= 1.16.0 required; use --with-quickfix=PREFIX for a CMake installation])
    ])
    AS_IF([test -z "$QUICKFIX_DICT"], [
      QUICKFIX_DICT=`$PKG_CONFIG --variable=prefix quickfix`/share/quickfix/FIX44.xml
    ])
  ], [
    QUICKFIX_CFLAGS="-I$with_quickfix/include"
    QUICKFIX_LIBS="-L$with_quickfix/lib -Wl,-rpath,$with_quickfix/lib -lquickfix"
    AS_IF([test -z "$QUICKFIX_DICT"], [QUICKFIX_DICT="$with_quickfix/share/quickfix/FIX44.xml"])
  ])
  z_quickfix_CPPFLAGS=$CPPFLAGS
  z_quickfix_LIBS=$LIBS
  CPPFLAGS="$QUICKFIX_CFLAGS $CPPFLAGS"
  LIBS="$QUICKFIX_LIBS $LIBS"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <quickfix/config.h>
#include <quickfix/DataDictionary.h>
#include <quickfix/SSLSocketInitiator.h>
#ifndef HAVE_SSL
#error QuickFIX must be built with SSL
#endif
  ]], [[FIX::DataDictionary dictionary; return dictionary.isMsgType("D");]])],
    [z_have_quickfix=yes], [AC_MSG_ERROR([QuickFIX headers/library with SSL required])])
  CPPFLAGS=$z_quickfix_CPPFLAGS
  LIBS=$z_quickfix_LIBS
  AS_IF([test ! -f "$QUICKFIX_DICT"], [AC_MSG_ERROR([Canonical FIX44.xml not found: $QUICKFIX_DICT])])
])
AC_SUBST(QUICKFIX_CFLAGS)
AC_SUBST(QUICKFIX_LIBS)
AM_CONDITIONAL([HAVE_QUICKFIX], [test "x$z_have_quickfix" = "xyes"])

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

# sqlite3 (enabled by default)
AC_ARG_WITH([sqlite],
  AS_HELP_STRING([--without-sqlite], [Build without SQLite3]), [],
  [with_sqlite=yes])
AS_IF([test ".$with_sqlite" = ".yes"], [
  PKG_CHECK_MODULES([SQLITE], [sqlite3 >= 3.37.0], [], [dnl
    AC_MSG_ERROR([sqlite3 >= 3.37.0 unavailable])
  ])
  AC_SUBST(SQLITE_CFLAGS)
  AC_SUBST(SQLITE_LIBS)
])
AM_CONDITIONAL([SQLITE], [test ".$with_sqlite" = ".yes"])

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

for i in zu zm zt ze zf zi zxdp zutcp zpb zfb zv zdbus ztcp ztls zfix zquic zhttp zgrpc zrest zjrpc zmcp zws zdb zdb_pq zdb_sqlite ztc zum zdf zrl zgtk zdash; do {
  case $i in
    zxdp|zutcp) test "$z_linux" = yes || continue;;
  esac
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
	zf/Makefile zf/src/Makefile zf/test/Makefile \
	zi/Makefile zi/src/Makefile zi/test/Makefile zi/itest/Makefile \
	zi/example/Makefile])
AS_IF([test "$z_linux" = yes], [
  AC_CONFIG_FILES([zxdp/Makefile zxdp/src/Makefile zxdp/test/Makefile \
	zxdp/util/Makefile zxdp/itest/Makefile \
	zutcp/Makefile zutcp/src/Makefile zutcp/test/Makefile \
	zutcp/util/Makefile zutcp/itest/Makefile zutcp/example/Makefile \
	zutcp/interop/Makefile zutcp/bench/Makefile])
])
AC_CONFIG_FILES([\
	zpb/Makefile zpb/src/Makefile zpb/test/Makefile zpb/itest/Makefile \
	zpb/example/Makefile zpb/interop/Makefile \
	zfb/Makefile zfb/src/Makefile zfb/test/Makefile zfb/itest/Makefile \
	zfb/interop/Makefile \
	zv/Makefile zv/src/Makefile zv/test/Makefile zv/interop/Makefile \
	zdbus/Makefile zdbus/src/Makefile zdbus/test/Makefile \
	zdbus/util/Makefile zdbus/itest/Makefile zdbus/example/Makefile \
	zdbus/interop/Makefile \
	zfix/Makefile zfix/src/Makefile zfix/test/Makefile \
	zfix/util/Makefile zfix/itest/Makefile zfix/example/Makefile \
	zfix/interop/Makefile zfix/bench/Makefile \
	ztcp/Makefile ztcp/src/Makefile ztcp/test/Makefile ztcp/example/Makefile \
	ztls/Makefile ztls/src/Makefile ztls/test/Makefile ztls/itest/Makefile ztls/example/Makefile \
	ztls/interop/Makefile \
	zquic/Makefile zquic/src/Makefile zquic/test/Makefile \
	zquic/itest/Makefile zquic/example/Makefile \
	zhttp/Makefile zhttp/src/Makefile zhttp/test/Makefile \
	zhttp/util/Makefile zhttp/itest/Makefile zhttp/interop/Makefile zhttp/bench/Makefile \
	zgrpc/Makefile zgrpc/src/Makefile zgrpc/src/Zgrpc.pc zgrpc/test/Makefile \
	zgrpc/itest/Makefile zgrpc/interop/Makefile \
	zrest/Makefile zrest/src/Makefile zrest/test/Makefile \
	zrest/itest/Makefile zrest/example/Makefile zrest/interop/Makefile \
	zjrpc/Makefile zjrpc/src/Makefile zjrpc/test/Makefile zjrpc/itest/Makefile \
	zjrpc/example/Makefile \
	zmcp/Makefile zmcp/src/Makefile zmcp/test/Makefile \
	zmcp/itest/Makefile zmcp/example/Makefile zmcp/interop/Makefile \
	zws/Makefile zws/src/Makefile zws/test/Makefile \
	zws/bench/Makefile zws/interop/Makefile \
	zdb/Makefile zdb/src/Makefile zdb/test/Makefile zdb/itest/Makefile \
	zdb/example/Makefile zdb/interop/Makefile \
	zdb_pq/Makefile zdb_pq/src/Makefile zdb_pq/test/Makefile \
	zdb_pq/itest/Makefile zdb_pq/bench/Makefile \
	zdb_sqlite/Makefile zdb_sqlite/src/Makefile zdb_sqlite/test/Makefile \
	zdb_sqlite/itest/Makefile zdb_sqlite/bench/Makefile \
	ztc/Makefile ztc/src/Makefile ztc/test/Makefile ztc/itest/Makefile \
	ztc/example/Makefile ztc/interop/Makefile \
	zum/Makefile zum/src/Makefile zum/test/Makefile zum/itest/Makefile \
	zum/example/Makefile zum/interop/Makefile \
	zdf/Makefile zdf/src/Makefile zdf/test/Makefile zdf/itest/Makefile \
	zrl/Makefile zrl/src/Makefile zrl/test/Makefile zrl/example/Makefile \
	zgtk/Makefile zgtk/src/Makefile zgtk/test/Makefile \
	zgtk/itest/Makefile \
	zgtk/example/Makefile zdash/Makefile zdash/src/Makefile \
	zdash/test/Makefile zdash/itest/Makefile])
AC_OUTPUT
