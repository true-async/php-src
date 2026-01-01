dnl config.m4 for extension mm_test

PHP_ARG_ENABLE([mm-test],
  [whether to enable mm_test support],
  [AS_HELP_STRING([--enable-mm-test],
    [Enable mm_test support (Memory Manager testing extension)])])

if test "$PHP_MM_TEST" != "no"; then
  dnl Check for ZTS - REQUIRED
  if test "$PHP_THREAD_SAFETY" != "yes"; then
    AC_MSG_ERROR([mm_test extension requires ZTS. Please configure with --enable-zts])
  fi

  AC_DEFINE(HAVE_MM_TEST_ZTS, 1, [Whether ZTS is enabled])
  PHP_NEW_EXTENSION(mm_test, mm_test.c, $ext_shared)
fi
