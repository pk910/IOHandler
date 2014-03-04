/* IOSSLBackend.c - IOMultiplexer
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
#define _IOHandler_internals
#include "IOInternal.h"
#include "IOHandler.h"
#include "IOLog.h"
#include "IOSockets.h"
#include "IOSSLBackend.h"

#ifdef HAVE_OPENSSL_SSL_H
/* OpenSSL Backend */


void iossl_init() {
    SSL_library_init();
    OpenSSL_add_all_algorithms(); /* load & register all cryptos, etc. */
    SSL_load_error_strings();
}

static void iossl_error() {
    unsigned long e;
    while((e = ERR_get_error())) {
        iolog_trigger(IOLOG_ERROR, "SSLv23 ERROR %lu: %s", e, ERR_error_string(e, NULL));
    }
}

// Client
void iossl_connect(struct _IOSocket *iosock) {
    struct IOSSLDescriptor *sslnode = malloc(sizeof(*sslnode));
    sslnode->sslContext = SSL_CTX_new(SSLv23_client_method());
    if(!sslnode->sslContext) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not create client SSL CTX");
        goto ssl_connect_err;
    }
    sslnode->sslHandle = SSL_new(sslnode->sslContext);
    if(!sslnode->sslHandle) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not create client SSL Handle");
        goto ssl_connect_err;
    }
    if(!SSL_set_fd(sslnode->sslHandle, iosock->fd)) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not set client fd in SSL Handle");
        goto ssl_connect_err;
    }
    SSL_set_connect_state(sslnode->sslHandle);
    iosock->sslnode = sslnode;
	iosock->socket_flags |= IOSOCKETFLAG_SSL_HANDSHAKE;
    iossl_client_handshake(iosock);
    return;
ssl_connect_err:
    free(sslnode);
    iosocket_events_callback(iosock, 0, 0);
}

void iossl_client_handshake(struct _IOSocket *iosock) {
    // Perform an SSL handshake.
    int ret = SSL_do_handshake(iosock->sslnode->sslHandle);
    iosock->socket_flags &= ~IOSOCKETFLAG_SSL_WANTWRITE;
    switch(SSL_get_error(iosock->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
            iolog_trigger(IOLOG_DEBUG, "SSL handshake for fd %d successful", iosock->fd);
			iosock->socket_flags |= IOSOCKETFLAG_SSL_ESTABLISHED;
            iosocket_events_callback(iosock, 0, 0); //perform IOEVENT_CONNECTED event
            break;
        case SSL_ERROR_WANT_READ:
            iolog_trigger(IOLOG_DEBUG, "SSL_do_handshake for fd %d returned SSL_ERROR_WANT_READ", iosock->fd);
            break;
        case SSL_ERROR_WANT_WRITE:
            iosock->socket_flags |= IOSOCKETFLAG_SSL_WANTWRITE;
            iolog_trigger(IOLOG_DEBUG, "SSL_do_handshake for fd %d returned SSL_ERROR_WANT_WRITE", iosock->fd);
            break;
        default:
            iolog_trigger(IOLOG_ERROR, "SSL_do_handshake for fd %d failed with ", iosock->fd);
            iosocket_events_callback(iosock, 0, 0);
            break;
    }
}


// Server
void iossl_listen(struct _IOSocket *iosock, const char *certfile, const char *keyfile) {
    struct IOSSLDescriptor *sslnode = malloc(sizeof(*sslnode));
    sslnode->sslContext = SSL_CTX_new(SSLv23_server_method());
    if(!sslnode->sslContext) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not create server SSL CTX");
        goto ssl_listen_err;
    }
    /* load certificate */
    if(SSL_CTX_use_certificate_file(sslnode->sslContext, certfile, SSL_FILETYPE_PEM) <= 0) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not load server certificate (%s)", certfile);
        goto ssl_listen_err;
    }
    /* load keyfile */
    if(SSL_CTX_use_PrivateKey_file(sslnode->sslContext, keyfile, SSL_FILETYPE_PEM) <= 0) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not load server keyfile (%s)", keyfile);
        goto ssl_listen_err;
    }
    /* check certificate and keyfile */
    if(!SSL_CTX_check_private_key(sslnode->sslContext)) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: server certificate (%s) and keyfile (%s) doesn't match!", certfile, keyfile);
        goto ssl_listen_err;
    }
    iosock->sslnode = sslnode;
    iosock->socket_flags |= IOSOCKETFLAG_SSL_ESTABLISHED;
    return;
ssl_listen_err:
    free(sslnode);
    iosock->sslnode = NULL;
    iosocket_events_callback(iosock, 0, 0);
}

void iossl_client_accepted(struct _IOSocket *iosock, struct _IOSocket *new_iosock) {
    struct IOSSLDescriptor *sslnode = malloc(sizeof(*sslnode));
    sslnode->sslHandle = SSL_new(sslnode->sslContext);
    if(!sslnode->sslHandle) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not create client SSL Handle");
        goto ssl_accept_err;
    }
    if(!SSL_set_fd(sslnode->sslHandle, new_iosock->fd)) {
        iossl_error();
        iolog_trigger(IOLOG_ERROR, "SSL: could not set client fd in SSL Handle");
        goto ssl_accept_err;
    }
    new_iosock->sslnode = sslnode;
    new_iosock->socket_flags |= IOSOCKETFLAG_SSL_HANDSHAKE;
    return;
ssl_accept_err:
    free(sslnode);
    iosock->sslnode = NULL;
    iosocket_events_callback(new_iosock, 0, 0);
}

void iossl_server_handshake(struct _IOSocket *iosock) {
    // Perform an SSL handshake.
    int ret = SSL_accept(iosock->sslnode->sslHandle);
    iosock->socket_flags &= ~IOSOCKETFLAG_SSL_WANTWRITE;
    switch(SSL_get_error(iosock->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
            iolog_trigger(IOLOG_DEBUG, "SSL handshake for fd %d successful", iosock->fd);
			iosock->socket_flags |= IOSOCKETFLAG_SSL_ESTABLISHED;
            iosocket_events_callback(iosock, 0, 0); //perform IOEVENT_CONNECTED event
            break;
        case SSL_ERROR_WANT_READ:
            iolog_trigger(IOLOG_DEBUG, "SSL_do_handshake for fd %d returned SSL_ERROR_WANT_READ", iosock->fd);
            break;
        case SSL_ERROR_WANT_WRITE:
            iosock->socket_flags |= IOSOCKETFLAG_SSL_WANTWRITE;
            iolog_trigger(IOLOG_DEBUG, "SSL_do_handshake for fd %d returned SSL_ERROR_WANT_WRITE", iosock->fd);
            break;
        default:
            iolog_trigger(IOLOG_ERROR, "SSL_do_handshake for fd %d failed with ", iosock->fd);
            iosocket_events_callback(iosock, 0, 0);
            break;
    }
}

void iossl_disconnect(struct _IOSocket *iosock) {
    if(!iosock->sslnode) return;
    SSL_shutdown(iosock->sslnode->sslHandle);
    SSL_free(iosock->sslnode->sslHandle);
    SSL_CTX_free(iosock->sslnode->sslContext);
    free(iosock->sslnode);
    iosock->sslnode = NULL;
    iosock->socket_flags &= ~IOSOCKETFLAG_SSLSOCKET;
}

int iossl_read(struct _IOSocket *iosock, char *buffer, int len) {
    if((iosock->socket_flags & (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED)) != (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED))
		return 0;
    int ret = SSL_read(iosock->sslnode->sslHandle, buffer, len);
    iosock->socket_flags &= ~(IOSOCKETFLAG_SSL_WANTWRITE | IOSOCKETFLAG_SSL_READHS);
    switch(SSL_get_error(iosock->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
        case SSL_ERROR_ZERO_RETURN:
            break;
        case SSL_ERROR_WANT_READ:
			iosock->socket_flags |= IOSOCKETFLAG_SSL_READHS;
            iolog_trigger(IOLOG_DEBUG, "SSL_read for fd %d returned SSL_ERROR_WANT_READ", iosock->fd);
            errno = EAGAIN;
            ret = -1;
            break;
        case SSL_ERROR_WANT_WRITE:
            iosock->socket_flags |= IOSOCKETFLAG_SSL_WANTWRITE | IOSOCKETFLAG_SSL_READHS;
            iolog_trigger(IOLOG_DEBUG, "SSL_read for fd %d returned SSL_ERROR_WANT_WRITE", iosock->fd);
            errno = EAGAIN;
            ret = -1;
            break;
        default:
            iolog_trigger(IOLOG_ERROR, "SSL_read for fd %d failed with ", iosock->fd);
            ret = -1;
            break;
    }
    return ret;
}

int iossl_write(struct _IOSocket *iosock, char *buffer, int len) {
    if((iosock->socket_flags & (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED)) != (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED))
		return 0;
    int ret = SSL_write(iosock->sslnode->sslHandle, buffer, len);
    iosock->socket_flags &= ~(IOSOCKETFLAG_SSL_WANTWRITE | IOSOCKETFLAG_SSL_WRITEHS);
    switch(SSL_get_error(iosock->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
        case SSL_ERROR_ZERO_RETURN:
            break;
        case SSL_ERROR_WANT_READ:
            iosock->socket_flags |= IOSOCKETFLAG_SSL_WRITEHS;
            iolog_trigger(IOLOG_DEBUG, "SSL_write for fd %d returned SSL_ERROR_WANT_READ", iosock->fd);
            errno = EAGAIN;
            ret = -1;
            break;
        case SSL_ERROR_WANT_WRITE:
            iosock->socket_flags |= IOSOCKETFLAG_SSL_WANTWRITE | IOSOCKETFLAG_SSL_WRITEHS;
            iolog_trigger(IOLOG_DEBUG, "SSL_write for fd %d returned SSL_ERROR_WANT_WRITE", iosock->fd);
            errno = EAGAIN;
            ret = -1;
            break;
        default:
            iolog_trigger(IOLOG_ERROR, "SSL_write for fd %d failed with ", iosock->fd);
            ret = -1;
            break;
    }
    return ret;
}

#else
// NULL-backend

void iossl_init() {};
void iossl_connect(struct _IOSocket *iosock) {};
void iossl_listen(struct _IOSocket *iosock, const char *certfile, const char *keyfile) {};
void iossl_client_handshake(struct _IOSocket *iosock) {};
void iossl_client_accepted(struct _IOSocket *iosock, struct IODescriptor *client_iofd) {};
void iossl_server_handshake(struct _IOSocket *iosock) {};
void iossl_disconnect(struct _IOSocket *iosock) {};
int iossl_read(struct _IOSocket *iosock, char *buffer, int len) { return 0; };
int iossl_write(struct _IOSocket *iosock, char *buffer, int len) { return 0; };
#endif
