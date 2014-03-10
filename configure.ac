# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([IOMultiplexer], [2.0], [iohandler@pk910.de], [pk910], [http://pk910.de])
AC_PREFIX_DEFAULT([~/iotest])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_HEADERS([config.h])

#LT_INIT([disable-static])

AC_MSG_RESULT($MODULES)
AC_SUBST(MODULES)

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB

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

AC_CHECK_LIB(ws2_32, main, [ LIBS="$LIBS -lws2_32" ], [])
AC_CHECK_LIB(ssl, SSL_read, [
  AC_CHECK_LIB(crypto, X509_new, [
    AC_CHECK_HEADERS(openssl/ssl.h openssl/err.h openssl/rand.h, [
      LIBS="$LIBS -lssl -lcrypto"
    ])
  ])
])
AC_CHECK_LIB(pthread, pthread_create, [
  AC_CHECK_HEADERS(pthread.h, [
    LIBS="$LIBS -lpthread"
  ])
])

AC_CONFIG_FILES([Makefile src/Makefile src/IOHandler/Makefile src/IOHandler++/Makefile src/IOHandler_test/Makefile src/IOHandler_test/socket/Makefile src/IOHandler_test/socket++/Makefile src/IOHandler_test/timer/Makefile])
AC_OUTPUT
