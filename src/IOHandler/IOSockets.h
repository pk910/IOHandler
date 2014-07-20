/* IOSockets.h - IOMultiplexer v2
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
#ifndef _IOSockets_h
#define _IOSockets_h
#include <sys/time.h>
#include <stddef.h>
#include "IODNSAddress.struct.h"

struct IOSocketBuffer {
	char *buffer;
	size_t bufpos, buflen;
};

#ifndef _IOHandler_internals
#include "IOHandler.h"
#else

struct _IOSocket;
struct IOSocket;
struct IOSocketBuffer;
struct IOSSLDescriptor;
struct _IODNSQuery;
struct IODNSEvent;

struct IOEngine {
	const char *name;
	int (*init)(void);
	void (*add)(struct _IOSocket *iosock);
	void (*remove)(struct _IOSocket *iosock);
	void (*update)(struct _IOSocket *iosock);
	void (*loop)(struct timeval *timeout);
	void (*cleanup)(void);
};

/* IO Engines */
extern struct IOEngine engine_select; /* select system call (should always be useable) */
extern struct IOEngine engine_kevent;
extern struct IOEngine engine_epoll;
extern struct IOEngine engine_win32;


/* _IOSocket linked list */
extern struct _IOSocket *iosocket_first;
extern struct _IOSocket *iosocket_last;

/* _IOSocket socket_flags */
#define IOSOCKETFLAG_ACTIVE           0x00000001
#define IOSOCKETFLAG_LISTENING        0x00000002
#define IOSOCKETFLAG_IPV6SOCKET       0x00000004
#define IOSOCKETFLAG_INCOMING         0x00000008 /* incoming (accepted) connection */
#define IOSOCKETFLAG_CONNECTING       0x00000010
#define IOSOCKETFLAG_SHUTDOWN         0x00000020 /* disconnect pending */
#define IOSOCKETFLAG_DEAD             0x00000040 /* socket dead (disconnected) */
#define IOSOCKETFLAG_RECONNECT_IPV4   0x00000080 /* possible fallback to ipv4 connect if ipv6 fails */

/* DNS Flags */
#define IOSOCKETFLAG_PENDING_BINDDNS  0x00000100
#define IOSOCKETFLAG_PENDING_DESTDNS  0x00000200
#define IOSOCKETFLAG_DNSDONE_BINDDNS  0x00000400
#define IOSOCKETFLAG_DNSDONE_DESTDNS  0x00000800
#define IOSOCKETFLAG_DNSERROR         0x00001000

/* SSL Flags */
#define IOSOCKETFLAG_SSLSOCKET        0x00002000 /* use ssl after connecting */
#define IOSOCKETFLAG_SSL_HANDSHAKE    0x00004000 /* SSL Handshake in progress */
#define IOSOCKETFLAG_SSL_WANTWRITE    0x00008000 /* ssl module wants write access */
#define IOSOCKETFLAG_SSL_READHS       0x00010000 /* ssl read rehandshake */
#define IOSOCKETFLAG_SSL_WRITEHS      0x00020000 /* ssl write rehandshake */
#define IOSOCKETFLAG_SSL_ESTABLISHED  0x00040000 /* ssl connection established */

/* WANT_READ / WANT_WRITE override */
#define IOSOCKETFLAG_OVERRIDE_WANT_RW 0x00080000
#define IOSOCKETFLAG_OVERRIDE_WANT_R  0x00100000
#define IOSOCKETFLAG_OVERRIDE_WANT_W  0x00200000

/* _IOSocket socket_flags */
#define IOSOCKETFLAG_DYNAMIC_BIND     0x00400000

/* Parent descriptors */
#define IOSOCKETFLAG_PARENT_PUBLIC    0x10000000
#define IOSOCKETFLAG_PARENT_DNSENGINE 0x20000000
/* reserved flags for additional descriptor types? */

struct IOSocketDNSLookup {
	unsigned int bindlookup : 1;
	char hostname[256];
	struct _IOSocket *iosocket;
	struct _IODNSQuery *query;
	struct IODNSResult *result;
};

struct _IOSocket {
	int fd;
	
	unsigned int socket_flags : 32;
	
	struct {
		struct IODNSAddress addr;
		struct IOSocketDNSLookup *addrlookup;
	} bind;
	struct {
		struct IODNSAddress addr;
		struct IOSocketDNSLookup *addrlookup;
	} dest;
	
	unsigned int port : 16;
	
	struct IOSocketBuffer readbuf;
	struct IOSocketBuffer writebuf;
	
	struct IOSSLDescriptor *sslnode;
	
	void *parent;
	
	struct _IOSocket *next, *prev;
};

void _init_sockets();
struct _IOSocket *_create_socket();
void _free_socket(struct _IOSocket *iosock);
void iosocket_activate(struct _IOSocket *iosock);
void iosocket_deactivate(struct _IOSocket *iosock);
void iosocket_update(struct _IOSocket *iosock);

void iosocket_loop(int usec);
void iosocket_lookup_callback(struct IOSocketDNSLookup *lookup, struct IODNSEvent *event);
void iosocket_events_callback(struct _IOSocket *iosock, int readable, int writeable);

int iosocket_wants_reads(struct _IOSocket *iosock);
int iosocket_wants_writes(struct _IOSocket *iosock);

#endif

struct IOSocketEvent;

#define IOSOCKET_CALLBACK(NAME) void NAME(struct IOSocketEvent *event)
typedef IOSOCKET_CALLBACK(iosocket_callback);

enum IOSocketStatus { 
	IOSOCKET_CLOSED, /* descriptor is dead (socket waiting for removal or timer) */
	IOSOCKET_LISTENING, /* descriptor is waiting for connections (server socket) */
	IOSOCKET_CONNECTING, /* descriptor is waiting for connection approval (connecting client socket) */
	IOSOCKET_CONNECTED, /* descriptor is connected (connected client socket) */
	IOSOCKET_SSLHANDSHAKE /* descriptor is waiting for ssl (handshake) */
};

enum IOSocketEventType {
	IOSOCKETEVENT_IGNORE,
	IOSOCKETEVENT_RECV, /* client socket received something (read_lines == 1  =>  recv_str valid;  read_lines == 0  =>  recv_buf valid) */
	IOSOCKETEVENT_CONNECTED, /* client socket connected successful */
	IOSOCKETEVENT_NOTCONNECTED, /* client socket could not connect (errid valid) */
	IOSOCKETEVENT_CLOSED, /* client socket lost connection (errid valid) */
	IOSOCKETEVENT_ACCEPT, /* server socket accepted new connection (accept_socket valid) */
	IOSOCKETEVENT_DNSFAILED /* failed to lookup DNS information (recv_str contains error message) */
};

#define IOSOCKET_ADDR_IPV4 0x01
#define IOSOCKET_ADDR_IPV6 0x02 /* overrides IOSOCKET_ADDR_IPV4 */
#define IOSOCKET_PROTO_UDP 0x04

#if !defined IOSOCKET_CPP
struct IOSocket {
	void *iosocket;
	
	struct IODNSAddress *remoteaddr;
	struct IODNSAddress *localaddr;
	
	enum IOSocketStatus status;
	int listening : 1;
	int ssl : 1;
	int ipv6 : 1;
	int parse_delimiter : 1;
	int parse_empty : 1; /* parse "empty" lines (only if parse_delimiter is set) */
	unsigned char delimiters[IOSOCKET_PARSE_DELIMITERS_COUNT];
	
	void *data;
	iosocket_callback *callback;
};

struct IOSocketEvent {
	enum IOSocketEventType type;
	struct IOSocket *socket;
	union {
		char *recv_str;
		struct IOSocketBuffer *recv_buf;
		int errid;
		struct IOSocket *accept_socket;
	} data;
};

struct IOSocket *iosocket_connect(const char *hostname, unsigned int port, int ssl, const char *bindhost, iosocket_callback *callback);
struct IOSocket *iosocket_connect_flags(const char *hostname, unsigned int port, int ssl, const char *bindhost, iosocket_callback *callback, int flags);
struct IOSocket *iosocket_listen(const char *hostname, unsigned int port, iosocket_callback *callback);
struct IOSocket *iosocket_listen_flags(const char *hostname, unsigned int port, iosocket_callback *callback, int flags);
struct IOSocket *iosocket_listen_ssl(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iosocket_callback *callback);
struct IOSocket *iosocket_listen_ssl_flags(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iosocket_callback *callback, int flags);
void iosocket_write(struct IOSocket *iosocket, const char *line);
void iosocket_send(struct IOSocket *iosocket, const char *data, size_t datalen);
void iosocket_printf(struct IOSocket *iosocket, const char *text, ...);
void iosocket_close(struct IOSocket *iosocket);

#endif
#endif
