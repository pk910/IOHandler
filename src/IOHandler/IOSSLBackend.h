/* IOHandler_SSL.h - IOMultiplexer
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
#ifndef _IOSSLBackend_h
#define _IOSSLBackend_h

struct _IOSocket;

#if defined(HAVE_GNUTLS_GNUTLS_H)
#include <gnutls/gnutls.h>
struct IOSSLDescriptor {
	union {
		struct {
			gnutls_session_t session;
			gnutls_certificate_client_credentials credentials;
		} client;
		struct {
			gnutls_priority_t priority;
			gnutls_certificate_credentials_t credentials;
		} server;
	} ssl;
};

#elif defined(HAVE_OPENSSL_SSL_H)
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

struct IOSSLDescriptor {
	SSL *sslHandle;
	SSL_CTX *sslContext;
};
#else
struct IOSSLDescriptor {
	//just unused
};
#endif

void iossl_init();
void iossl_connect(struct _IOSocket *iosock);
void iossl_listen(struct _IOSocket *iosock, const char *certfile, const char *keyfile);
void iossl_client_handshake(struct _IOSocket *iosock);
void iossl_client_accepted(struct _IOSocket *iosock, struct _IOSocket *new_iosock);
void iossl_server_handshake(struct _IOSocket *iosock);
void iossl_disconnect(struct _IOSocket *iosock);
int iossl_read(struct _IOSocket *iosock, char *buffer, int len);
int iossl_write(struct _IOSocket *iosock, char *buffer, int len);

#endif
