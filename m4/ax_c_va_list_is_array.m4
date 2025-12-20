AC_DEFUN([AX_C_VA_LIST_IS_ARRAY], [
  AC_CACHE_CHECK([whether va_list assignments need array notation],
		 ax_cv_va_list_is_array, [AC_RUN_IFELSE([AC_LANG_SOURCE([
#include <stdlib.h>
#include <stdarg.h>
void foo(int i, ...) {
  va_list ap1, ap2;
  va_start(ap1, i);
  ap2 = ap1;
  if (va_arg(ap2, int) != 42 || va_arg(ap1, int) != 42) { exit(1); }
  va_end(ap1);
}
int main() {
 foo(0, 42);
 return 0;
}])],
      [ax_cv_va_list_is_array=false],
      [ax_cv_va_list_is_array=true],
      [ax_cv_va_list_is_array=false])])
  if test ".$ax_cv_va_list_is_array" = ".yes"; then
    AC_DEFINE([HAVE_VA_LIST_IS_ARRAY], [1],
              [Define to 1 if va_list assignments need array notation])
  fi
])

