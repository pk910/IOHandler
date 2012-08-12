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

void iohandler_ssl_init() {
#ifdef HAVE_SSL
    SSL_library_init();
    SSL_load_error_strings();
#endif
}

void iohandler_ssl_connect(struct IODescriptor *iofd) {
#ifdef HAVE_SSL
    iofd->state = IO_SSLWAIT;
    struct IOSSLNode *sslnode = malloc(sizeof(*sslnode));
    sslnode->sslContext = SSL_CTX_new(SSLv23_client_method());
    if(!sslnode->sslContext)
        goto ssl_connect_err;
    sslnode->sslHandle = SSL_new(sslnode->sslContext);
    if(!sslnode->sslHandle) 
        goto ssl_connect_err;
    if(!SSL_set_fd(sslnode->sslHandle, iofd->fd))
        goto ssl_connect_err;
    SSL_set_connect_state(sslnode->sslHandle);
    iofd->sslnode = sslnode;
    iohandler_ssl_client_handshake(iofd);
    return;
ssl_connect_err:
    free(sslnode);
    iohandler_events(iofd, 0, 0);
#endif    
}

void iohandler_ssl_client_handshake(struct IODescriptor *iofd) {
#ifdef HAVE_SSL
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
#endif
}

void iohandler_ssl_disconnect(struct IODescriptor *iofd) {
#ifdef HAVE_SSL
    if(!iofd->sslnode) return;
    SSL_shutdown(iofd->sslnode->sslHandle);
    SSL_free(iofd->sslnode->sslHandle);
    SSL_CTX_free(iofd->sslnode->sslContext);
    free(iofd->sslnode);
    iofd->sslnode = NULL;
    iofd->ssl_active = 0;
#endif
}

int iohandler_ssl_read(struct IODescriptor *iofd, char *buffer, int len) {
#ifdef HAVE_SSL
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
#endif
    return 0;
}

int iohandler_ssl_write(struct IODescriptor *iofd, char *buffer, int len) {
#ifdef HAVE_SSL
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
#endif
    return 0;
}
