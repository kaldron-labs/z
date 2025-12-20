AC_DEFUN([AX_PATH_JNI], [dnl
  AC_MSG_CHECKING([JNI])
  AC_ARG_WITH(jni,
    [AS_HELP_STRING([--with-jni=prefix@<:@:prefix:...@:>@], [specify location(s) of JNI include files])],,
    with_jni="yes")
  if test ".$with_jni" = ".no" ; then
    AC_MSG_RESULT([disabled])
    m4_ifval($2,$2)
  else
    AC_MSG_RESULT([(testing)])
    case "$build_os" in
      linux-*) JNI_PLATFORM="linux" ;;
      *) JNI_PLATFORM="win32" ;;
    esac
    OLDCPPFLAGS="$CPPFLAGS";
    JNI_INCDIRS="";
    if test ".$with_jni" != "." -a ".$with_jni" != ".yes"; then
      for i in `echo $with_jni | sed -e 's/:/ /g'`; do
	JNI_INCDIRS="$JNI_INCDIRS -I$i/include -I$i/include/$JNI_PLATFORM"
      done
    fi
    CPPFLAGS="$JNI_INCDIRS $CPPFLAGS"
    AC_RUN_IFELSE([
      AC_LANG_SOURCE([[
#include <stdlib.h>
#include <stdio.h>
#include <jni.h>
int main()
{
  puts("yes");
  return 0;
}
      ]])
    ], [
      ax_jni_ok=`./conftest$ac_exeext`
    ],[],[])
    CPPFLAGS="$OLDCPPFLAGS"
    if test ".$ax_jni_ok" = ".yes"; then
      JNI_CPPFLAGS="$JNI_INCDIRS"
      AC_MSG_NOTICE([JNI_CPPFLAGS=$JNI_CPPFLAGS])
      AC_MSG_CHECKING([JNI])
      AC_MSG_RESULT([yes])
      m4_ifval($1,$1)
    else
      AC_MSG_CHECKING([JNI])
      AC_MSG_RESULT([no])
      m4_ifval($2,$2)
    fi
  fi
])
