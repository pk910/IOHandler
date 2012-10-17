/* IOHandler_SSL.h - IOMultiplexer
 * Copyright (C) 2012  Philipp Kreil (pk910)
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
#ifndef _IOHandler_SSL_h
#define _IOHandler_SSL_h

struct IODescriptor;

#ifdef HAVE_SSL
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

struct IOSSLNode {
    SSL *sslHandle;
    SSL_CTX *sslContext;
};
#else
struct IOSSLNode {
    //just unused
};
#endif

void iohandler_ssl_init();
void iohandler_ssl_connect(struct IODescriptor *iofd);
void iohandler_ssl_listen(struct IODescriptor *iofd, const char *certfile, const char *keyfile);
void iohandler_ssl_client_handshake(struct IODescriptor *iofd);
void iohandler_ssl_client_accepted(struct IODescriptor *iofd, struct IODescriptor *client_iofd);
void iohandler_ssl_server_handshake(struct IODescriptor *iofd);
void iohandler_ssl_disconnect(struct IODescriptor *iofd);
int iohandler_ssl_read(struct IODescriptor *iofd, char *buffer, int len);
int iohandler_ssl_write(struct IODescriptor *iofd, char *buffer, int len);

#endif
