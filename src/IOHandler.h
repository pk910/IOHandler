/* IOHandler.h - IOMultiplexer
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
#ifndef _IOHandler_h
#define _IOHandler_h
#include <stddef.h>
#include <sys/time.h> /* struct timeval */

#define IO_READ_BUFLEN 1024
#define IO_MAX_TIMEOUT 10
#define IO_LINE_LEN    1024

struct timeval;
struct IODescriptor;
struct IOEvent;
struct IOSSLNode;

enum IOLogType {
    IOLOG_DEBUG,
    IOLOG_WARNING,
    IOLOG_ERROR,
    IOLOG_FATAL
};

#define IOHANDLER_CALLBACK(NAME) void NAME(struct IOEvent *event)
typedef IOHANDLER_CALLBACK(iohandler_callback);

#define IOHANDLER_LOG_BACKEND(NAME) void NAME(enum IOLogType type, const char *line)
typedef IOHANDLER_LOG_BACKEND(iohandler_log_callback);

extern iohandler_log_callback *iolog_backend;

enum IOType {
    IOTYPE_UNKNOWN, /* ignore descriptor (uninitialized) */
    IOTYPE_SERVER, /* server socket */
    IOTYPE_CLIENT, /* client socket */
    IOTYPE_STDIN, /* stdin */
    IOTYPE_TIMER /* timer */
};

enum IOStatus { 
    IO_CLOSED, /* descriptor is dead (socket waiting for removal or timer) */
    IO_LISTENING, /* descriptor is waiting for connections (server socket) */
    IO_CONNECTING, /* descriptor is waiting for connection approval (connecting client socket) */
    IO_CONNECTED, /* descriptor is connected (connected client socket) */
    IO_SSLWAIT /* waiting for SSL backend (e.g. handshake) */
};

enum IOEventType {
    IOEVENT_IGNORE,
    IOEVENT_READABLE, /* socket is readable - not read anything yet, could also be disconnect notification */
    IOEVENT_RECV, /* client socket received something (recv_str valid) */
    IOEVENT_CONNECTED, /* client socket connected successful */
    IOEVENT_NOTCONNECTED, /* client socket could not connect (errid valid) */
    IOEVENT_CLOSED, /* client socket lost connection (errid valid) */
    IOEVENT_ACCEPT, /* server socket accepted new connection (accept_fd valid) */
    IOEVENT_SSLACCEPT, /* SSL server socket accepted new connection (accept_iofd valid) */
    IOEVENT_TIMEOUT, /* timer timed out */
    IOEVENT_SSLFAILED /* failed to initialize SSL session */
};

struct IOBuffer {
    char *buffer;
    size_t bufpos, buflen;
};

struct IODescriptor {
    int fd;
    enum IOType type;
    enum IOStatus state;
    struct timeval timeout;
    iohandler_callback *callback;
    struct IOBuffer readbuf;
    struct IOBuffer writebuf;
    void *data;
    int read_lines : 1;
    int ssl : 1;
    int ssl_server_hs : 1;
    int ssl_active : 1;
    int ssl_hs_read : 1;
    int ssl_hs_write : 1;
    struct IOSSLNode *sslnode;
    
    struct IODescriptor *next, *prev;
};

struct IOEvent {
    enum IOEventType type;
    struct IODescriptor *iofd;
    union {
        char *recv_str;
        int accept_fd;
        int errid;
        struct IODescriptor *accept_iofd;
    } data;
};

#define IOHANDLER_LISTEN_IPV4 0x01
#define IOHANDLER_LISTEN_IPV6 0x02 /* overrides IOHANDLER_LISTEN_IPV4 */

#define IOHANDLER_CONNECT_IPV4 0x01
#define IOHANDLER_CONNECT_IPV6 0x02 /* overrides IOHANDLER_CONNECT_IPV4 */

struct IODescriptor *iohandler_add(int sockfd, enum IOType type, struct timeval *timeout, iohandler_callback *callback);
struct IODescriptor *iohandler_timer(struct timeval timeout, iohandler_callback *callback);
struct IODescriptor *iohandler_connect(const char *hostname, unsigned int port, int ssl, const char *bind, iohandler_callback *callback);
struct IODescriptor *iohandler_connect_flags(const char *hostname, unsigned int port, int ssl, const char *bindhost, iohandler_callback *callback, int flags);
struct IODescriptor *iohandler_listen(const char *hostname, unsigned int port, iohandler_callback *callback);
struct IODescriptor *iohandler_listen_flags(const char *hostname, unsigned int port, iohandler_callback *callback, int flags);
struct IODescriptor *iohandler_listen_ssl(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iohandler_callback *callback);
struct IODescriptor *iohandler_listen_ssl_flags(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iohandler_callback *callback, int flags);
void iohandler_write(struct IODescriptor *iofd, const char *line);
void iohandler_send(struct IODescriptor *iofd, const char *data, size_t datalen);
void iohandler_printf(struct IODescriptor *iofd, const char *text, ...);
void iohandler_close(struct IODescriptor *iofd);
void iohandler_update(struct IODescriptor *iofd);
void iohandler_set_timeout(struct IODescriptor *iofd, struct timeval *timeout);

void iohandler_poll();
void iohandler_poll_timeout(struct timeval timeout);

#endif
