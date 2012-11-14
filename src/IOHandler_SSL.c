/* IOHandler_SSL.c - IOMultiplexer
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
#include "IOEngine.h"
#include "IOHandler_SSL.h"
#ifdef HAVE_SSL

void iohandler_ssl_init() {
    SSL_library_init();
    OpenSSL_add_all_algorithms(); /* load & register all cryptos, etc. */
    SSL_load_error_strings();
}

static void iohandler_ssl_error() {
    unsigned long e;
    while((e = ERR_get_error())) {
        iohandler_log(IOLOG_ERROR, "SSLv23 ERROR %lu: %s", e, ERR_error_string(e, NULL));
    }
}

void iohandler_ssl_connect(struct IODescriptor *iofd) {
    iofd->state = IO_SSLWAIT;
    iofd->ssl_server_hs = 0;
    struct IOSSLNode *sslnode = malloc(sizeof(*sslnode));
    sslnode->sslContext = SSL_CTX_new(SSLv23_client_method());
    if(!sslnode->sslContext) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not create client SSL CTX");
        goto ssl_connect_err;
    }
    sslnode->sslHandle = SSL_new(sslnode->sslContext);
    if(!sslnode->sslHandle) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not create client SSL Handle");
        goto ssl_connect_err;
    }
    if(!SSL_set_fd(sslnode->sslHandle, iofd->fd)) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not set client fd in SSL Handle");
        goto ssl_connect_err;
    }
    SSL_set_connect_state(sslnode->sslHandle);
    iofd->sslnode = sslnode;
    iohandler_ssl_client_handshake(iofd);
    return;
ssl_connect_err:
    free(sslnode);
    iohandler_events(iofd, 0, 0);
}

void iohandler_ssl_listen(struct IODescriptor *iofd, const char *certfile, const char *keyfile) {
    struct IOSSLNode *sslnode = malloc(sizeof(*sslnode));
    sslnode->sslContext = SSL_CTX_new(SSLv23_server_method());
    if(!sslnode->sslContext) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not create server SSL CTX");
        goto ssl_listen_err;
    }
    /* load certificate */
    if(SSL_CTX_use_certificate_file(sslnode->sslContext, certfile, SSL_FILETYPE_PEM) <= 0) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not load server certificate (%s)", certfile);
        goto ssl_listen_err;
    }
    /* load keyfile */
    if(SSL_CTX_use_PrivateKey_file(sslnode->sslContext, keyfile, SSL_FILETYPE_PEM) <= 0) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not load server keyfile (%s)", keyfile);
        goto ssl_listen_err;
    }
    /* check certificate and keyfile */
    if(!SSL_CTX_check_private_key(sslnode->sslContext)) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: server certificate (%s) and keyfile (%s) doesn't match!", certfile, keyfile);
        goto ssl_listen_err;
    }
    iofd->sslnode = sslnode;
    return;
ssl_listen_err:
    free(sslnode);
    iofd->sslnode = NULL;
    iohandler_events(iofd, 0, 0);
}

void iohandler_ssl_client_handshake(struct IODescriptor *iofd) {
    // Perform an SSL handshake.
    int ret = SSL_do_handshake(iofd->sslnode->sslHandle);
    iofd->ssl_hs_read = 0;
    iofd->ssl_hs_write = 0;
    switch(SSL_get_error(iofd->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
            iofd->state = IO_CONNECTING;
            iofd->ssl_active = 1;
            iohandler_log(IOLOG_DEBUG, "SSL handshake for %s (fd: %d) successful", iohandler_iotype_name(iofd->type), iofd->fd);
            iohandler_events(iofd, 0, 1); //perform IOEVENT_CONNECTED event
            break;
        case SSL_ERROR_WANT_READ:
            iofd->ssl_hs_read = 1;
            iohandler_log(IOLOG_DEBUG, "SSL_do_handshake for %s (fd: %d) returned SSL_ERROR_WANT_READ", iohandler_iotype_name(iofd->type), iofd->fd);
            break;
        case SSL_ERROR_WANT_WRITE:
            iofd->ssl_hs_write = 1;
            iohandler_log(IOLOG_DEBUG, "SSL_do_handshake for %s (fd: %d) returned SSL_ERROR_WANT_WRITE", iohandler_iotype_name(iofd->type), iofd->fd);
            break;
        default:
            iohandler_log(IOLOG_ERROR, "SSL_do_handshake for %s (fd: %d) failed with ", iohandler_iotype_name(iofd->type), iofd->fd);
            iohandler_events(iofd, 0, 0);
            break;
    }
}

void iohandler_ssl_client_accepted(struct IODescriptor *iofd, struct IODescriptor *client_iofd) {
    struct IOSSLNode *sslnode = malloc(sizeof(*sslnode));
    sslnode->sslHandle = SSL_new(sslnode->sslContext);
    if(!sslnode->sslHandle) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not create client SSL Handle");
        goto ssl_accept_err;
    }
    if(!SSL_set_fd(sslnode->sslHandle, client_iofd->fd)) {
        iohandler_ssl_error();
        iohandler_log(IOLOG_ERROR, "SSL: could not set client fd in SSL Handle");
        goto ssl_accept_err;
    }
    client_iofd->state = IO_SSLWAIT;
    client_iofd->ssl_server_hs = 1;
    client_iofd->ssl = 1;
    client_iofd->sslnode = sslnode;
    client_iofd->callback = iofd->callback;
    client_iofd->data = iofd;
    return;
ssl_accept_err:
    iohandler_close(client_iofd);
    free(sslnode);
}

void iohandler_ssl_server_handshake(struct IODescriptor *iofd) {
    // Perform an SSL handshake.
    int ret = SSL_accept(iofd->sslnode->sslHandle);
    iofd->ssl_hs_read = 0;
    iofd->ssl_hs_write = 0;
    switch(SSL_get_error(iofd->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
            iofd->state = IO_CONNECTING;
            iofd->ssl_active = 1;
            iohandler_log(IOLOG_DEBUG, "SSL handshake for %s (fd: %d) successful", iohandler_iotype_name(iofd->type), iofd->fd);
            iohandler_events(iofd, 0, 1); //perform IOEVENT_CONNECTED event
            break;
        case SSL_ERROR_WANT_READ:
            iofd->ssl_hs_read = 1;
            iohandler_log(IOLOG_DEBUG, "SSL_do_handshake for %s (fd: %d) returned SSL_ERROR_WANT_READ", iohandler_iotype_name(iofd->type), iofd->fd);
            break;
        case SSL_ERROR_WANT_WRITE:
            iofd->ssl_hs_write = 1;
            iohandler_log(IOLOG_DEBUG, "SSL_do_handshake for %s (fd: %d) returned SSL_ERROR_WANT_WRITE", iohandler_iotype_name(iofd->type), iofd->fd);
            break;
        default:
            iohandler_log(IOLOG_ERROR, "SSL_do_handshake for %s (fd: %d) failed with ", iohandler_iotype_name(iofd->type), iofd->fd);
            iohandler_events(iofd, 0, 0);
            break;
    }
}

void iohandler_ssl_disconnect(struct IODescriptor *iofd) {
    if(!iofd->sslnode) return;
    SSL_shutdown(iofd->sslnode->sslHandle);
    SSL_free(iofd->sslnode->sslHandle);
    SSL_CTX_free(iofd->sslnode->sslContext);
    free(iofd->sslnode);
    iofd->sslnode = NULL;
    iofd->ssl_active = 0;
}

int iohandler_ssl_read(struct IODescriptor *iofd, char *buffer, int len) {
    if(!iofd->sslnode) return 0;
    int ret = SSL_read(iofd->sslnode->sslHandle, buffer, len);
    int update = (iofd->ssl_hs_read || iofd->ssl_hs_write);
    iofd->ssl_hs_read = 0;
    iofd->ssl_hs_write = 0;
    switch(SSL_get_error(iofd->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
        case SSL_ERROR_ZERO_RETURN:
            if(update)
                iohandler_update(iofd);
            return ret;
            break;
        case SSL_ERROR_WANT_READ:
            iofd->ssl_hs_read = 1;
            iohandler_update(iofd);
            iohandler_log(IOLOG_DEBUG, "SSL_read for %s (fd: %d) returned SSL_ERROR_WANT_READ", iohandler_iotype_name(iofd->type), iofd->fd);
            errno = EAGAIN;
            return -1;
            break;
        case SSL_ERROR_WANT_WRITE:
            iofd->ssl_hs_write = 1;
            iohandler_update(iofd);
            iohandler_log(IOLOG_DEBUG, "SSL_read for %s (fd: %d) returned SSL_ERROR_WANT_WRITE", iohandler_iotype_name(iofd->type), iofd->fd);
            errno = EAGAIN;
            return -1;
            break;
        default:
            iohandler_log(IOLOG_ERROR, "SSL_read for %s (fd: %d) failed with ", iohandler_iotype_name(iofd->type), iofd->fd);
            return -1;
            break;
    }
}

int iohandler_ssl_write(struct IODescriptor *iofd, char *buffer, int len) {
    if(!iofd->sslnode) return 0;
    int ret = SSL_write(iofd->sslnode->sslHandle, buffer, len);
    int update = (iofd->ssl_hs_read || iofd->ssl_hs_write);
    iofd->ssl_hs_read = 0;
    iofd->ssl_hs_write = 0;
    switch(SSL_get_error(iofd->sslnode->sslHandle, ret)) {
        case SSL_ERROR_NONE:
        case SSL_ERROR_ZERO_RETURN:
            if(update)
                iohandler_update(iofd);
            return ret;
            break;
        case SSL_ERROR_WANT_READ:
            iofd->ssl_hs_read = 1;
            iohandler_update(iofd);
            iohandler_log(IOLOG_DEBUG, "SSL_write for %s (fd: %d) returned SSL_ERROR_WANT_READ", iohandler_iotype_name(iofd->type), iofd->fd);
            errno = EAGAIN;
            return -1;
            break;
        case SSL_ERROR_WANT_WRITE:
            iofd->ssl_hs_write = 1;
            iohandler_update(iofd);
            iohandler_log(IOLOG_DEBUG, "SSL_write for %s (fd: %d) returned SSL_ERROR_WANT_WRITE", iohandler_iotype_name(iofd->type), iofd->fd);
            errno = EAGAIN;
            return -1;
            break;
        default:
            iohandler_log(IOLOG_ERROR, "SSL_write for %s (fd: %d) failed with ", iohandler_iotype_name(iofd->type), iofd->fd);
            return -1;
            break;
    }
}

#else
// NULL-backend

void iohandler_ssl_init() {};
void iohandler_ssl_connect(struct IODescriptor *iofd) {};
void iohandler_ssl_listen(struct IODescriptor *iofd, const char *certfile, const char *keyfile) {};
void iohandler_ssl_client_handshake(struct IODescriptor *iofd) {};
void iohandler_ssl_client_accepted(struct IODescriptor *iofd, struct IODescriptor *client_iofd) {};
void iohandler_ssl_server_handshake(struct IODescriptor *iofd) {};
void iohandler_ssl_disconnect(struct IODescriptor *iofd) {};
int iohandler_ssl_read(struct IODescriptor *iofd, char *buffer, int len) { return 0; };
int iohandler_ssl_write(struct IODescriptor *iofd, char *buffer, int len) { return 0; };
#endif
