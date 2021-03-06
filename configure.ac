# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([IOMultiplexer], [2.0], [iohandler@pk910.de], [pk910], [http://pk910.de])
AC_PREFIX_DEFAULT([~/iotest])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_HEADERS([config.h])

LT_INIT([disable-static])

AC_MSG_RESULT($MODULES)
AC_SUBST(MODULES)

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug], [debug mode (compile using -O0 -Wall -Wshadow -Werror)])],
  [CFLAGS='-g -O0 -Wall -Wshadow -Werror'],
  [CFLAGS='-g -O2']
)

# Checks for libraries.

CFLAGS="$CFLAGS -D_GNU_SOURCE"

AC_FUNC_MALLOC
AC_CHECK_FUNCS([usleep select socket inet_pton inet_ntop])
AC_CHECK_HEADERS([fcntl.h sys/socket.h sys/select.h sys/time.h sys/types.h unistd.h windows.h winsock2.h errno.h sys/epoll.h sys/event.h])



AC_CHECK_LIB(ws2_32, main, [ 
  LIBS="$LIBS -lws2_32" 
  is_win32="yes"
], [
  is_win32="no"
])

have_gnutls="no"
AC_CHECK_LIB(gnutls, gnutls_init, [
  AC_CHECK_HEADERS(gnutls/gnutls.h, [
    LIBS="$LIBS -lgnutls"
    have_gnutls="yes"
  ])
])
if test x"$have_gnutls" = xno; then
  if test x$is_win32 = xyes ; then
    openssl_deps="-lcrypto -lgdi32"
  else
    openssl_deps="-lcrypto"
  fi
  AC_CHECK_LIB([ssl],[SSL_library_init], [
    LIBS="$LIBS -lssl $openssl_deps"
  ], [AC_MSG_ERROR([OpenSSL libraries required])], $openssl_deps)
fi

AC_CHECK_LIB(pthread, pthread_create, [
  AC_CHECK_HEADERS(pthread.h, [
    LIBS="$LIBS -lpthread"
  ])
])
AC_CHECK_LIB(cares, ares_init, [
  AC_CHECK_HEADERS(ares.h, [
    LIBS="$LIBS -lcares"
  ])
])


AC_CONFIG_FILES([
  Makefile
  src/Makefile
  src/IOHandler/Makefile
  src/IOHandler++/Makefile
  src/IOHandler_test/Makefile
  src/IOHandler_test/client/Makefile
  src/IOHandler_test/client++/Makefile
  src/IOHandler_test/client_ssl/Makefile
  src/IOHandler_test/server/Makefile
  src/IOHandler_test/server_ssl/Makefile
  src/IOHandler_test/timer/Makefile
  src/IOHandler_test/timer++/Makefile
  src/IOHandler_test/resolv/Makefile
])
AC_OUTPUT
