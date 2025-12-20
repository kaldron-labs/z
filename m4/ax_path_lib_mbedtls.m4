dnl @synopsis AX_PATH_LIB_MBEDTLS [(A/NA)]
dnl
dnl check for mbedtls and set MBEDTLS_LIBS, MBEDTLS_LDFLAGS, MBEDTLS_CPPFLAGS
dnl
dnl also provide --with-mbedtls option that may point to the $prefix of
dnl the mbedtls installation - the macro will check $mbedtls/include and
dnl $mbedtls/lib to contain the necessary files.
dnl
dnl the usual two ACTION-IF-FOUND / ACTION-IF-NOT-FOUND are supported
dnl and they can take advantage of the LDFLAGS/CPPFLAGS additions.

AC_DEFUN([AX_PATH_LIB_MBEDTLS], [
  AC_MSG_CHECKING([lib mbedtls])
  AC_ARG_WITH(mbedtls, [AS_HELP_STRING([--with-mbedtls=prefix],
				    [override location of mbedtls library])],,
	      with_mbedtls="yes")
  if test ".$with_mbedtls" = ".no" ; then
    AC_MSG_RESULT([disabled])
    m4_ifval($2,$2)
  else
    AC_MSG_RESULT([(testing)])
    OLDLIBS="$LIBS"
    OLDCPPFLAGS="$CPPFLAGS"
    OLDLDFLAGS="$LDFLAGS"
    if test ".$with_mbedtls" != "." -a ".$with_mbedtls" != ".yes"; then
      CPPFLAGS="-I$with_mbedtls/include $CPPFLAGS"
      MBEDTLS_LDFLAGS_="-L$with_mbedtls/lib -L$with_mbedtls/lib64"
      case "$build_os" in
	mingw-*) ;;
	*) MBEDTLS_LDFLAGS_="$MBEDTLS_LDFLAGS_ -Wl,-R,$with_mbedtls/lib -Wl,-R,$with_mbedtls/lib64" ;;
      esac
      LDFLAGS="$MBEDTLS_LDFLAGS_ $LDFLAGS"
    fi
    AC_CHECK_LIB(mbedtls, mbedtls_ssl_init)
    LIBS="$OLDLIBS"
    CPPFLAGS="$OLDCPPFLAGS"
    LDFLAGS="$OLDLDFLAGS"
    if test ".$ac_cv_lib_mbedtls_mbedtls_ssl_init" = ".yes"; then
      MBEDTLS_LIBS="-lmbedtls -lmbedx509 -lmbedcrypto"
      MBEDTLS_LDFLAGS=""
      MBEDTLS_CPPFLAGS=""
      if test ".$with_mbedtls" != "." -a ".$with_mbedtls" != ".yes"; then
	AC_MSG_NOTICE([MBEDTLS_LDFLAGS=$MBEDTLS_LDFLAGS_])
	MBEDTLS_LDFLAGS="$MBEDTLS_LDFLAGS_"
	if test -d "$with_mbedtls/include"; then
	  AC_MSG_NOTICE([MBEDTLS_CPPFLAGS=-I$with_mbedtls/include])
	  MBEDTLS_CPPFLAGS="-I$with_mbedtls/include"
	fi
      fi
      AC_MSG_CHECKING([lib mbedtls])
      AC_MSG_RESULT([$MBEDTLS_LIBS])
      m4_ifval($1,$1)
    else
      AC_MSG_CHECKING([lib mbedtls])
      AC_MSG_RESULT([no])
      m4_ifval($2,$2)
    fi
  fi
])
