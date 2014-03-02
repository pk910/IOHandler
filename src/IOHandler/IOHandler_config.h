/* IOHanlder_config.h - IOMultiplexer
 * Copyright (C) 2014  Philipp Kreil (pk910)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>. 
 */

/* required configure script checks
 AC_FUNC_MALLOC
 AC_FUNC_CALLOC
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
*/
// configure config file
#include "../../config.h"

#define IOHANDLER_MAX_SOCKETS 1024
#define IOHANDLER_LOOP_MAXTIME 100000 /* 100ms */

#define IOSOCKET_PARSE_DELIMITERS_COUNT 5
#define IOSOCKET_PARSE_LINE_LIMIT 1024
#define IOSOCKET_PRINTF_LINE_LEN  1024

//#define IODNS_USE_THREADS

#define IOGC_TIMEOUT 60
