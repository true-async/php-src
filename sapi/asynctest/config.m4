PHP_ARG_ENABLE([asynctest],
  [for asynctest SAPI],
  [AS_HELP_STRING([--enable-asynctest],
    [Build asynctest SAPI: C-level cmocka tests for ext/async])],
  [no],
  [no])

dnl PHP_ASYNCTEST_TARGET(name, target-var)
dnl One binary per test file: sapi/asynctest/tests/test-NAME
AC_DEFUN([PHP_ASYNCTEST_TARGET], [
  PHP_ASYNCTEST_BINARIES="$PHP_ASYNCTEST_BINARIES sapi/asynctest/tests/test-$1"
  PHP_SUBST([$2])
  PHP_ADD_SOURCES_X([sapi/asynctest/tests], [test_$1.c], [], [$2])
  $2="[$]$2 $PHP_ASYNCTEST_COMMON_OBJS"
])

if test "$PHP_ASYNCTEST" != "no"; then
  AS_VAR_IF([enable_async], [yes],, [AC_MSG_ERROR([asynctest SAPI requires --enable-async])])
  AS_VAR_IF([enable_zts], [yes],, [AC_MSG_ERROR([asynctest SAPI requires --enable-zts (TrueAsync is ZTS-only)])])

  PKG_CHECK_MODULES([CMOCKA], [cmocka], [
    PHP_EVAL_INCLINE([$CMOCKA_CFLAGS])
  ], [AC_MSG_ERROR([cmocka not found (install libcmocka-dev)])])
  PHP_SUBST([CMOCKA_LIBS])

  PHP_ADD_MAKEFILE_FRAGMENT([$abs_srcdir/sapi/asynctest/Makefile.frag])

  PHP_ADD_BUILD_DIR([sapi/asynctest])
  PHP_ADD_BUILD_DIR([sapi/asynctest/tests])
  PHP_BINARIES="$PHP_BINARIES asynctest"
  PHP_INSTALLED_SAPIS="$PHP_INSTALLED_SAPIS asynctest"

  PHP_ASYNCTEST_BINARIES=""
  PHP_ADD_SOURCES_X([sapi/asynctest],
    [asynctest_sapi.c reactor_harness.c],
    [], [PHP_ASYNCTEST_COMMON_OBJS])

  PHP_ASYNCTEST_TARGET([listen_uaf], [PHP_ASYNCTEST_LISTEN_UAF_OBJS])
  PHP_ASYNCTEST_TARGET([timer_overflow], [PHP_ASYNCTEST_TIMER_OVERFLOW_OBJS])

  PHP_SUBST([PHP_ASYNCTEST_COMMON_OBJS])
  PHP_SUBST([PHP_ASYNCTEST_BINARIES])
fi
