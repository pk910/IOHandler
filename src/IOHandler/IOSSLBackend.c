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

#if defined(HAVE_GNUTLS_GNUTLS_H)
#include <string.h>
#include <stdlib.h>
#include <errno.h>
/* GnuTLS Backend */
static gnutls_dh_params_t dh_params;
static unsigned int dh_params_bits;

static int generate_dh_params() {
	dh_params_bits = gnutls_sec_param_to_pk_bits(GNUTLS_PK_DH, GNUTLS_SEC_PARAM_LEGACY);
	gnutls_dh_params_init(&dh_params);
	gnutls_dh_params_generate2(dh_params, dh_params_bits);
	return 0;
}

void iossl_init() {
	int ret;
	if((ret = gnutls_global_init()) != GNUTLS_E_SUCCESS) {
		iolog_trigger(IOLOG_ERROR, "SSL: gnutls_global_init(): failed (%d)", ret);
		//TODO: Error handling?
		return;
	}
	generate_dh_params();
}

// Client
void iossl_connect(struct _IOSocket *iosock) {
	struct IOSSLDescriptor *sslnode = malloc(sizeof(*sslnode));
	int err;
	
	err = gnutls_certificate_allocate_credentials(&sslnode->ssl.client.credentials);
	if(err < 0) {
		goto ssl_connect_err;
	}
	
	gnutls_init(&sslnode->ssl.client.session, GNUTLS_CLIENT);
	
	gnutls_priority_set_direct(sslnode->ssl.client.session, "SECURE128:+SECURE192:-VERS-TLS-ALL:+VERS-TLS1.2", NULL);
	gnutls_credentials_set(sslnode->ssl.client.session, GNUTLS_CRD_CERTIFICATE, sslnode->ssl.client.credentials);
	
	gnutls_transport_set_int(sslnode->ssl.client.session, iosock->fd);
	gnutls_handshake_set_timeout(sslnode->ssl.client.session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);
	
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
	int ret = gnutls_handshake(iosock->sslnode->ssl.client.session);
	iosock->socket_flags &= ~IOSOCKETFLAG_SSL_WANTWRITE;
	
	if(ret < 0) {
		if(gnutls_error_is_fatal(ret) == 0) {
			if(gnutls_record_get_direction(iosock->sslnode->ssl.client.session)) {
				iosock->socket_flags |= IOSOCKETFLAG_SSL_WANTWRITE;
				iolog_trigger(IOLOG_DEBUG, "gnutls_handshake for fd %d wants to write...", iosock->fd);
			} else {
				iolog_trigger(IOLOG_DEBUG, "gnutls_handshake for fd %d wants to read...", iosock->fd);
			}
		} else {
			iolog_trigger(IOLOG_ERROR, "gnutls_handshake for fd %d failed with %s", iosock->fd, gnutls_strerror(ret));
			iosocket_events_callback(iosock, 0, 0);
		}
	} else {
		char *desc;
		desc = gnutls_session_get_desc(iosock->sslnode->ssl.client.session);
		iolog_trigger(IOLOG_DEBUG, "SSL handshake for fd %d successful: %s", iosock->fd, desc);
		gnutls_free(desc);
		iosock->socket_flags |= IOSOCKETFLAG_SSL_ESTABLISHED;
		iosocket_events_callback(iosock, 0, 0); //perform IOEVENT_CONNECTED event
	}
}


// Server
void iossl_listen(struct _IOSocket *iosock, const char *certfile, const char *keyfile) {
	struct IOSSLDescriptor *sslnode = malloc(sizeof(*sslnode));
	
	gnutls_priority_init(&sslnode->ssl.server.priority, "SECURE128:+SECURE192:-VERS-TLS-ALL:+VERS-TLS1.2", NULL);
	
	gnutls_certificate_allocate_credentials(&sslnode->ssl.server.credentials);
	int ret = gnutls_certificate_set_x509_key_file(sslnode->ssl.server.credentials, certfile, keyfile, GNUTLS_X509_FMT_PEM);
	if (ret < 0) {
		iolog_trigger(IOLOG_ERROR, "SSL: could not load server certificate/key (%s %s): %d - %s", certfile, keyfile, ret, gnutls_strerror(ret));
		goto ssl_listen_err;
	}
	
	gnutls_certificate_set_dh_params(sslnode->ssl.server.credentials, dh_params);
	
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
	
	gnutls_init(&sslnode->ssl.client.session, GNUTLS_SERVER);
	gnutls_priority_set(sslnode->ssl.client.session, iosock->sslnode->ssl.server.priority);
	gnutls_credentials_set(sslnode->ssl.client.session, GNUTLS_CRD_CERTIFICATE, iosock->sslnode->ssl.server.credentials);
	gnutls_dh_set_prime_bits(sslnode->ssl.client.session, dh_params_bits);
	
	/* We don't request any certificate from the client.
	 * If we did we would need to verify it.
	 */
	gnutls_certificate_server_set_request(sslnode->ssl.client.session, GNUTLS_CERT_IGNORE);
	
	gnutls_transport_set_int(sslnode->ssl.client.session, new_iosock->fd);
	
	new_iosock->sslnode = sslnode;
	new_iosock->socket_flags |= IOSOCKETFLAG_SSL_HANDSHAKE;
	return;
	/*
ssl_accept_err:
	free(sslnode);
	iosock->sslnode = NULL;
	iosocket_events_callback(new_iosock, 0, 0);
	*/
}

void iossl_server_handshake(struct _IOSocket *iosock) {
	return iossl_client_handshake(iosock);
}

void iossl_disconnect(struct _IOSocket *iosock) {
	if(!iosock->sslnode) return;
	
	if((iosock->socket_flags & IOSOCKETFLAG_LISTENING)) {
		gnutls_certificate_free_credentials(iosock->sslnode->ssl.server.credentials);
		gnutls_priority_deinit(iosock->sslnode->ssl.server.priority);
	} else {
		gnutls_bye(iosock->sslnode->ssl.client.session, GNUTLS_SHUT_RDWR);
		if(!(iosock->socket_flags & IOSOCKETFLAG_INCOMING))
			gnutls_certificate_free_credentials(iosock->sslnode->ssl.client.credentials);
		gnutls_deinit(iosock->sslnode->ssl.client.session);
	}
	
	free(iosock->sslnode);
	iosock->sslnode = NULL;
	iosock->socket_flags &= ~IOSOCKETFLAG_SSLSOCKET;
}

static void iossl_rehandshake(struct _IOSocket *iosock, int hsflag) {
	int ret = gnutls_handshake(iosock->sslnode->ssl.client.session);
	iosock->socket_flags &= ~IOSOCKETFLAG_SSL_WANTWRITE;
	
	if(ret < 0) {
		if(gnutls_error_is_fatal(ret) == 0) {
			if(gnutls_record_get_direction(iosock->sslnode->ssl.client.session)) {
				iosock->socket_flags |= IOSOCKETFLAG_SSL_WANTWRITE | hsflag;
				iolog_trigger(IOLOG_DEBUG, "gnutls_handshake for fd %d wants to write...", iosock->fd);
			} else {
				iosock->socket_flags |= hsflag;
				iolog_trigger(IOLOG_DEBUG, "gnutls_handshake for fd %d wants to read...", iosock->fd);
			}
		} else {
			iolog_trigger(IOLOG_ERROR, "gnutls_handshake for fd %d failed: %s", iosock->fd, gnutls_strerror(ret));
			//TODO: Error Action?
		}
	} else {
		char *desc;
		desc = gnutls_session_get_desc(iosock->sslnode->ssl.client.session);
		iolog_trigger(IOLOG_DEBUG, "SSL handshake for fd %d successful: %s", iosock->fd, desc);
		gnutls_free(desc);
		iosock->socket_flags &= ~hsflag;
	}
}

int iossl_read(struct _IOSocket *iosock, char *buffer, int len) {
	if((iosock->socket_flags & (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED)) != (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED))
		return 0;
	if((iosock->socket_flags & IOSOCKETFLAG_SSL_READHS)) {
		iossl_rehandshake(iosock, IOSOCKETFLAG_SSL_READHS);
		errno = EAGAIN;
		return -1;
	}
	int ret = gnutls_record_recv(iosock->sslnode->ssl.client.session, buffer, len);
	if(ret == 0) {
		//TLS session closed
		//TODO: Action?
	} else if(ret < 0 && gnutls_error_is_fatal(ret) == 0) {
		iolog_trigger(IOLOG_WARNING, "gnutls_record_recv for fd %d returned %s", iosock->fd, gnutls_strerror(ret));
		if(ret == GNUTLS_E_REHANDSHAKE) {
			iossl_rehandshake(iosock, IOSOCKETFLAG_SSL_READHS);
			errno = EAGAIN;
			return -1;
		}
	} else if(ret < 0) {
		iolog_trigger(IOLOG_ERROR, "gnutls_record_recv for fd %d failed: %s", iosock->fd, gnutls_strerror(ret));
		errno = ret;
		return ret;
	}
	return ret;
}

int iossl_write(struct _IOSocket *iosock, char *buffer, int len) {
	if((iosock->socket_flags & (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED)) != (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_ESTABLISHED))
		return 0;
	if((iosock->socket_flags & IOSOCKETFLAG_SSL_WRITEHS)) {
		iossl_rehandshake(iosock, IOSOCKETFLAG_SSL_WRITEHS);
		errno = EAGAIN;
		return -1;
	}
	int ret = gnutls_record_send(iosock->sslnode->ssl.client.session, buffer, len);
	if(ret < 0 && gnutls_error_is_fatal(ret) == 0) {
		iolog_trigger(IOLOG_WARNING, "gnutls_record_send for fd %d returned %s", iosock->fd, gnutls_strerror(ret));
		if(ret == GNUTLS_E_REHANDSHAKE) {
			iossl_rehandshake(iosock, IOSOCKETFLAG_SSL_WRITEHS);
			errno = EAGAIN;
			return -1;
		}
	} else if(ret < 0) {
		iolog_trigger(IOLOG_ERROR, "gnutls_record_send for fd %d failed: %s", iosock->fd, gnutls_strerror(ret));
		errno = ret;
		return ret;
	}
	return ret;
}

#elif defined(HAVE_OPENSSL_SSL_H)
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
	sslnode->sslHandle = SSL_new(iosock->sslnode->sslContext);
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
	if(!(iosock->socket_flags & IOSOCKETFLAG_INCOMING))
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
	int err = SSL_get_error(iosock->sslnode->sslHandle, ret);
	switch(err) {
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
		case SSL_ERROR_SYSCALL:
			iolog_trigger(IOLOG_ERROR, "SSL_read for fd %d failed with SSL_ERROR_SYSCALL: %d", iosock->fd, errno);
			ret = -1;
			break;
		default:
			iolog_trigger(IOLOG_ERROR, "SSL_read for fd %d failed with %d", iosock->fd, err);
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
void iossl_client_accepted(struct _IOSocket *iosock, struct _IOSocket *client_iofd) {};
void iossl_server_handshake(struct _IOSocket *iosock) {};
void iossl_disconnect(struct _IOSocket *iosock) {};
int iossl_read(struct _IOSocket *iosock, char *buffer, int len) { return 0; };
int iossl_write(struct _IOSocket *iosock, char *buffer, int len) { return 0; };
#endif
