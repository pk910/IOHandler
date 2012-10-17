/* IOHandler.c - IOMultiplexer
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
#include "IOHandler.h"
#include "IOEngine.h"
#include "IOHandler_SSL.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#ifdef WIN32
#define _WIN32_WINNT 0x501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

#define MAXLOG 1024
iohandler_log_callback *iolog_backend = NULL;

struct IODescriptor *first_descriptor = NULL;
struct IODescriptor *timer_priority = NULL;

#ifdef HAVE_PTHREAD_H
static pthread_mutex_t io_thread_sync;
static pthread_mutex_t io_poll_sync;
#endif

void iohandler_log(enum IOLogType type, char *text, ...) {
    va_list arg_list;
    char logBuf[MAXLOG+1];
    int pos;
    logBuf[0] = '\0';
    va_start(arg_list, text);
    pos = vsnprintf(logBuf, MAXLOG - 1, text, arg_list);
    va_end(arg_list);
    if (pos < 0 || pos > (MAXLOG - 1)) pos = MAXLOG - 1;
    logBuf[pos] = '\n';
    logBuf[pos+1] = '\0';
    
    if(iolog_backend)
        iolog_backend(type, logBuf);
}

/* IO Engines */
extern struct IOEngine engine_select; /* select system call (should always be useable) */
extern struct IOEngine engine_kevent;
extern struct IOEngine engine_epoll;
extern struct IOEngine engine_win32;

struct IOEngine *engine = NULL;

static void iohandler_init_engine() {
    if(engine) return;
    IOTHREAD_MUTEX_INIT(io_thread_sync);
    IOTHREAD_MUTEX_INIT(io_poll_sync);
    
    //try other engines
    if(!engine && engine_kevent.init && engine_kevent.init())
        engine = &engine_kevent;
    if(!engine && engine_epoll.init && engine_epoll.init())
        engine = &engine_epoll;
    if(!engine && engine_win32.init && engine_win32.init())
        engine = &engine_win32;
    
    if (!engine) {
        if(engine_select.init())
            engine = &engine_select;
        else {
            iohandler_log(IOLOG_FATAL, "found no useable IO engine");
            return;
        }
    }
    iohandler_log(IOLOG_DEBUG, "using %s IO engine", engine->name);
    iohandler_ssl_init();
}

static void iohandler_append(struct IODescriptor *descriptor) {
    IOSYNCHRONIZE(io_thread_sync);
    struct timeval *timeout = ((descriptor->timeout.tv_sec || descriptor->timeout.tv_usec) ? &descriptor->timeout : NULL);
    if(timeout) {
        struct IODescriptor *iofd;
        int set_priority = 1;
        descriptor->timeout = *timeout;
        if(timer_priority)
            iofd = timer_priority;
        else
            iofd = first_descriptor;
        if(iofd) {
            for(;;iofd = iofd->next) {
                if(timeval_is_smaler(timeout, (&iofd->timeout))) {
                    descriptor->prev = iofd->prev;
                    descriptor->next = iofd;
                    if(iofd->prev)
                        iofd->prev->next = descriptor;
                    iofd->prev = descriptor;
                    if(set_priority)
                        timer_priority = descriptor;
                    break;
                }
                if(iofd == timer_priority)
                    set_priority = 0;
                if(iofd->next == NULL) {
                    descriptor->next = NULL;
                    descriptor->prev = iofd;
                    iofd->next = descriptor;
                    if(set_priority)
                        timer_priority = descriptor;
                    break;
                }
            }
        } else {
            descriptor->prev = NULL;
            descriptor->next = NULL;
            first_descriptor = descriptor;
            timer_priority = descriptor;
        }
        
    } else {
        descriptor->prev = NULL;
        descriptor->next = first_descriptor;
        if(first_descriptor)
            first_descriptor->prev = descriptor;
        first_descriptor = descriptor;
    }
    IODESYNCHRONIZE(io_thread_sync);
}

static void iohandler_remove(struct IODescriptor *descriptor, int engine_remove) {
    //remove IODescriptor from the list
    IOSYNCHRONIZE(io_thread_sync);
    if(descriptor->prev)
        descriptor->prev->next = descriptor->next;
    else
        first_descriptor = descriptor->next;
    if(descriptor->next)
        descriptor->next->prev = descriptor->prev;
    if(descriptor == timer_priority)
        timer_priority = descriptor->next;
    
    if(engine_remove)
        engine->remove(descriptor);
    if(descriptor->readbuf.buffer)
        free(descriptor->readbuf.buffer);
    if(descriptor->writebuf.buffer)
        free(descriptor->writebuf.buffer);
    iohandler_log(IOLOG_DEBUG, "removed IODescriptor (%d) of type `%s`", descriptor->fd, iohandler_iotype_name(descriptor->type));
    free(descriptor);
    IODESYNCHRONIZE(io_thread_sync);
}

struct IODescriptor *iohandler_add(int sockfd, enum IOType type, struct timeval *timeout, iohandler_callback *callback) {
    //just add a new IODescriptor
    struct IODescriptor *descriptor = calloc(1, sizeof(*descriptor));
    if(!descriptor) {
        iohandler_log(IOLOG_ERROR, "could not allocate memory for IODescriptor in %s:%d", __FILE__, __LINE__);
        return NULL;
    }
    descriptor->fd = (type == IOTYPE_STDIN ? fileno(stdin) : sockfd);
    descriptor->type = type;
    descriptor->state = (type == IOTYPE_STDIN ? IO_CONNECTED : IO_CLOSED);
    descriptor->callback = callback;
    if(timeout)
        descriptor->timeout = *timeout;
    if(type != IOTYPE_TIMER) {
        descriptor->readbuf.buffer = malloc(IO_READ_BUFLEN + 2);
        descriptor->readbuf.bufpos = 0;
        descriptor->readbuf.buflen = IO_READ_BUFLEN;
        descriptor->writebuf.buffer = malloc(IO_READ_BUFLEN + 2);
        descriptor->writebuf.bufpos = 0;
        descriptor->writebuf.buflen = IO_READ_BUFLEN;
    }
    
    if(!engine) {
        iohandler_init_engine();
        if(!engine) {
            return NULL;
        }
    }
    engine->add(descriptor);
    
    //add IODescriptor to the list
    iohandler_append(descriptor);
    
    iohandler_log(IOLOG_DEBUG, "added custom socket descriptor (%d) as type `%s`", sockfd, iohandler_iotype_name(type));
    return descriptor;
}

void iohandler_set_timeout(struct IODescriptor *descriptor, struct timeval *timeout) {
    if(descriptor->prev)
        descriptor->prev->next = descriptor->next;
    else
        first_descriptor = descriptor->next;
    if(descriptor->next)
        descriptor->next->prev = descriptor->prev;
    if(descriptor == timer_priority)
        timer_priority = descriptor->next;
    if(timeout) 
        descriptor->timeout = *timeout;
    else {
        descriptor->timeout.tv_sec = 0;
        descriptor->timeout.tv_usec = 0;
    }
    iohandler_append(descriptor);
}

static void iohandler_increase_iobuf(struct IOBuffer *iobuf, size_t required) {
    if(iobuf->buflen >= required) return;
    char *new_buf = realloc(iobuf->buffer, required + 2);
    if(new_buf) {
        iobuf->buffer = new_buf;
        iobuf->buflen = required;
    }
}

struct IODescriptor *iohandler_timer(struct timeval timeout, iohandler_callback *callback) {
    struct IODescriptor *descriptor;
    descriptor = iohandler_add(-1, IOTYPE_TIMER, &timeout, callback);
    if(!descriptor) {
        iohandler_log(IOLOG_ERROR, "could not allocate memory for IODescriptor in %s:%d", __FILE__, __LINE__);
        return NULL;
    }
    iohandler_log(IOLOG_DEBUG, "added timer descriptor (sec: %d; usec: %d)", timeout.tv_sec, timeout.tv_usec);
    return descriptor;
}

struct IODescriptor *iohandler_connect(const char *hostname, unsigned int port, int ssl, const char *bindhost, iohandler_callback *callback) {
    return iohandler_connect_flags(hostname, port, ssl, bindhost, callback, IOHANDLER_CONNECT_IPV4 | IOHANDLER_CONNECT_IPV6);
}

struct IODescriptor *iohandler_connect_flags(const char *hostname, unsigned int port, int ssl, const char *bindhost, iohandler_callback *callback, int flags) {
    //non-blocking connect
    int sockfd, result;
    struct addrinfo hints, *res;
    struct sockaddr_in *ip4 = NULL;
    struct sockaddr_in6 *ip6 = NULL;
    size_t dstaddrlen;
    struct sockaddr *dstaddr = NULL;
    struct IODescriptor *descriptor;
    
    if(!engine) {
        iohandler_init_engine();
        if(!engine) return NULL;
    }
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;
    if ((result = getaddrinfo (hostname, NULL, &hints, &res))) {
        iohandler_log(IOLOG_ERROR, "could not resolve %s to an IP address (%d)", hostname, result);
        return NULL;
    }
    while (res) {
        switch (res->ai_family) {
        case AF_INET:
            ip4 = (struct sockaddr_in *) res->ai_addr;
            break;
        case AF_INET6:
            ip6 = (struct sockaddr_in6 *) res->ai_addr;
            break;
        }
        res = res->ai_next;
        freeaddrinfo(res);
    }
    
    if(ip6 && (flags & IOHANDLER_CONNECT_IPV6)) {
        sockfd = socket(AF_INET6, SOCK_STREAM, 0);
        if(sockfd == -1) {
            iohandler_log(IOLOG_ERROR, "could not create socket in %s:%d", __FILE__, __LINE__);
            return NULL;
        }
        
        ip6->sin6_family = AF_INET6;
        ip6->sin6_port = htons(port);
        
        struct sockaddr_in6 *ip6vhost = NULL;
        if (bindhost && !getaddrinfo(bindhost, NULL, &hints, &res)) {
            while (res) {
                switch (res->ai_family) {
                case AF_INET6:
                    ip6vhost = (struct sockaddr_in6 *) res->ai_addr;
                    break;
                }
                res = res->ai_next;
                freeaddrinfo(res);
            }
        }
        if(ip6vhost) {
            ip6vhost->sin6_family = AF_INET6;
            ip6vhost->sin6_port = htons(0);
            bind(sockfd, (struct sockaddr*)ip6vhost, sizeof(*ip6vhost));
        }
        dstaddr = (struct sockaddr*)ip6;
        dstaddrlen = sizeof(*ip6);
    } else if(ip4 && (flags & IOHANDLER_CONNECT_IPV4)) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd == -1) {
            iohandler_log(IOLOG_ERROR, "could not create socket in %s:%d", __FILE__, __LINE__);
            return NULL;
        }
        
        ip4->sin_family = AF_INET;
        ip4->sin_port = htons(port);
        
        struct sockaddr_in *ip4vhost = NULL;
        if (bindhost && !getaddrinfo(bindhost, NULL, &hints, &res)) {
            while (res) {
                switch (res->ai_family) {
                case AF_INET:
                    ip4vhost = (struct sockaddr_in *) res->ai_addr;
                    break;
                }
                res = res->ai_next;
                freeaddrinfo(res);
            }
        }
        if(ip4vhost) {
            ip4vhost->sin_family = AF_INET;
            ip4vhost->sin_port = htons(0);
            bind(sockfd, (struct sockaddr*)ip4vhost, sizeof(*ip4vhost));
        }
        dstaddr = (struct sockaddr*)ip4;
        dstaddrlen = sizeof(*ip4);
    } else
        return NULL;
    //prevent SIGPIPE
    #ifndef WIN32
    #if defined(SO_NOSIGPIPE)
    {
        int set = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
    }
    #else
    signal(SIGPIPE, SIG_IGN);
    #endif
    #endif
    //make sockfd unblocking
    #if defined(F_GETFL)
    {
        int flags;
        flags = fcntl(sockfd, F_GETFL);
        fcntl(sockfd, F_SETFL, flags|O_NONBLOCK);
        flags = fcntl(sockfd, F_GETFD);
        fcntl(sockfd, F_SETFD, flags|FD_CLOEXEC);
    }
    #else
    /* I hope you're using the Win32 backend or something else that
     * automatically marks the file descriptor non-blocking...
     */
    #endif
    descriptor = iohandler_add(sockfd, IOTYPE_CLIENT, NULL, callback);
    if(!descriptor) {
        close(sockfd);
        return NULL;
    }
    connect(sockfd, dstaddr, dstaddrlen); //returns EINPROGRESS here (nonblocking)
    descriptor->state = IO_CONNECTING;
    descriptor->ssl = (ssl ? 1 : 0);
    descriptor->read_lines = 1;
    engine->update(descriptor);
    iohandler_log(IOLOG_DEBUG, "added client socket (%d) connecting to %s:%d", sockfd, hostname, port);
    return descriptor;
}

struct IODescriptor *iohandler_listen(const char *hostname, unsigned int port, iohandler_callback *callback) {
    return iohandler_listen_flags(hostname, port, callback, IOHANDLER_LISTEN_IPV4 | IOHANDLER_LISTEN_IPV6);
}

struct IODescriptor *iohandler_listen_flags(const char *hostname, unsigned int port, iohandler_callback *callback, int flags) {
    int sockfd;
    struct addrinfo hints, *res;
    struct sockaddr_in *ip4 = NULL;
    struct sockaddr_in6 *ip6 = NULL;
    struct IODescriptor *descriptor;
    unsigned int opt;
    
    if(!engine) {
        iohandler_init_engine();
        if(!engine) return NULL;
    }
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;
    if (getaddrinfo (hostname, NULL, &hints, &res)) {
        return NULL;
    }
    while (res) {
        switch (res->ai_family) {
        case AF_INET:
            ip4 = (struct sockaddr_in *) res->ai_addr;
            break;
        case AF_INET6:
            ip6 = (struct sockaddr_in6 *) res->ai_addr;
            break;
        }
        res = res->ai_next;
        freeaddrinfo(res);
    }
    
    if(ip6 && (flags & IOHANDLER_LISTEN_IPV6)) {
        sockfd = socket(AF_INET6, SOCK_STREAM, 0);
        if(sockfd == -1) return NULL;
        
        opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        
        ip6->sin6_family = AF_INET6;
        ip6->sin6_port = htons(port);
        
        bind(sockfd, (struct sockaddr*)ip6, sizeof(*ip6));
    } else if(ip4 && (flags && IOHANDLER_LISTEN_IPV4)) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd == -1) return NULL;
        
        opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        
        ip4->sin_family = AF_INET;
        ip4->sin_port = htons(port);
        
        bind(sockfd, (struct sockaddr*)ip4, sizeof(*ip4));
    } else
        return NULL;
    //prevent SIGPIPE
    #ifndef WIN32
    #if defined(SO_NOSIGPIPE)
    {
        int set = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
    }
    #else
    signal(SIGPIPE, SIG_IGN);
    #endif
    #endif
    //make sockfd unblocking
    #if defined(F_GETFL)
    {
        int flag;
        flag = fcntl(sockfd, F_GETFL);
        fcntl(sockfd, F_SETFL, flag|O_NONBLOCK);
        flag = fcntl(sockfd, F_GETFD);
        fcntl(sockfd, F_SETFD, flag|FD_CLOEXEC);
    }
    #else
    /* I hope you're using the Win32 backend or something else that
     * automatically marks the file descriptor non-blocking...
     */
    #endif
    descriptor = iohandler_add(sockfd, IOTYPE_SERVER, NULL, callback);
    if(!descriptor) {
        close(sockfd);
        return NULL;
    }
    listen(sockfd, 1);
    descriptor->state = IO_LISTENING;
    engine->update(descriptor);
    iohandler_log(IOLOG_DEBUG, "added server socket (%d) listening on %s:%d", sockfd, hostname, port);
    return descriptor;
}

struct IODescriptor *iohandler_listen_ssl(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iohandler_callback *callback) {
    return iohandler_listen_ssl_flags(hostname, port, certfile, keyfile, callback, IOHANDLER_LISTEN_IPV4 | IOHANDLER_LISTEN_IPV6);
}

struct IODescriptor *iohandler_listen_ssl_flags(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iohandler_callback *callback, int flags) {
    struct IODescriptor *descriptor = iohandler_listen_flags(hostname, port, callback, flags);
    if(!descriptor)
        return NULL;
    //SSL Server Socket
    iohandler_ssl_listen(descriptor, certfile, keyfile);
    if(descriptor->sslnode)
        descriptor->ssl = 1;
    return descriptor;
}

void iohandler_write(struct IODescriptor *iofd, const char *line) {
    size_t linelen = strlen(line);
    iohandler_send(iofd, line, linelen);
}

void iohandler_send(struct IODescriptor *iofd, const char *data, size_t datalen) {
    if(iofd->type == IOTYPE_TIMER || iofd->state == IO_CLOSED) {
        iohandler_log(IOLOG_ERROR, "could not write to socket (%s)", (iofd->type == IOTYPE_TIMER ? "IOTYPE_TIMER" : "IO_CLOSED"));
        return;
    }
    iohandler_log(IOLOG_DEBUG, "add %d to writebuf (fd: %d): %s", datalen, iofd->fd, data);
    if(iofd->writebuf.buflen < iofd->writebuf.bufpos + datalen) {
        iohandler_log(IOLOG_DEBUG, "increase writebuf (curr: %d) to %d (+%d bytes)", iofd->writebuf.buflen, iofd->writebuf.bufpos + datalen, (iofd->writebuf.bufpos + datalen - iofd->writebuf.buflen));
        iohandler_increase_iobuf(&iofd->writebuf, iofd->writebuf.bufpos + datalen);
        if(iofd->writebuf.buflen < iofd->writebuf.bufpos + datalen) {
            iohandler_log(IOLOG_ERROR, "increase writebuf (curr: %d) to %d (+%d bytes) FAILED", iofd->writebuf.buflen, iofd->writebuf.bufpos + datalen, (iofd->writebuf.bufpos + datalen - iofd->writebuf.buflen));
            return;
        }
    }
    memcpy(iofd->writebuf.buffer + iofd->writebuf.bufpos, data, datalen);
    iofd->writebuf.bufpos += datalen;
    engine->update(iofd);
}

void iohandler_printf(struct IODescriptor *iofd, const char *text, ...) {
    va_list arg_list;
    char sendBuf[IO_LINE_LEN];
    int pos;
    sendBuf[0] = '\0';
    va_start(arg_list, text);
    pos = vsnprintf(sendBuf, IO_LINE_LEN - 2, text, arg_list);
    va_end(arg_list);
    if (pos < 0 || pos > (IO_LINE_LEN - 2)) pos = IO_LINE_LEN - 2;
    sendBuf[pos] = '\n';
    sendBuf[pos+1] = '\0';
    iohandler_send(iofd, sendBuf, pos+1);
}

static int iohandler_try_write(struct IODescriptor *iofd) {
    if(!iofd->writebuf.bufpos) return 0;
    iohandler_log(IOLOG_DEBUG, "write writebuf (%d bytes) to socket (fd: %d)", iofd->writebuf.bufpos, iofd->fd);
    int res;
    if(iofd->ssl_active)
        res = iohandler_ssl_write(iofd, iofd->writebuf.buffer, iofd->writebuf.bufpos);
    else
        res = send(iofd->fd, iofd->writebuf.buffer, iofd->writebuf.bufpos, 0);
    if(res < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            iohandler_log(IOLOG_ERROR, "could not write to socket (fd: %d): %d - %s", iofd->fd, errno, strerror(errno));
        else
            res = 0;
    } else {
        iofd->writebuf.bufpos -= res;
        if(iofd->state != IO_CLOSED)
            engine->update(iofd);
    }
    return res;
}

void iohandler_close(struct IODescriptor *iofd) {
    int engine_remove = 1;
    iofd->state = IO_CLOSED;
    if(iofd->writebuf.bufpos) {
        //try to send everything before closing
#if defined(F_GETFL)
        {
            int flags;
            flags = fcntl(iofd->fd, F_GETFL);
            fcntl(iofd->fd, F_SETFL, flags & ~O_NONBLOCK);
            flags = fcntl(iofd->fd, F_GETFD);
            fcntl(iofd->fd, F_SETFD, flags|FD_CLOEXEC);
        }
#else
        engine_remove = 0;
        engine->remove(iofd);
#endif
        iohandler_try_write(iofd);
    }
    //close IODescriptor
    if(iofd->ssl)
        iohandler_ssl_disconnect(iofd);
    if(iofd->type == IOTYPE_SERVER || iofd->type == IOTYPE_CLIENT || iofd->type == IOTYPE_STDIN)
        close(iofd->fd);
    iohandler_remove(iofd, engine_remove);
}

void iohandler_update(struct IODescriptor *iofd) {
    iohandler_log(IOLOG_DEBUG, "external call to iohandler_update (fd: %d)", iofd->fd);
    engine->update(iofd);
}

static void iohandler_trigger_event(struct IOEvent *event) {
    if(!event->iofd->callback) return;
    iohandler_log(IOLOG_DEBUG, "triggering event (%s) for %s (fd: %d)", iohandler_ioeventtype_name(event->type), iohandler_iotype_name(event->iofd->type), event->iofd->fd);
    event->iofd->callback(event);
}

void iohandler_events(struct IODescriptor *iofd, int readable, int writeable) {
    struct IOEvent callback_event;
    callback_event.type = IOEVENT_IGNORE;
    callback_event.iofd = iofd;
    switch(iofd->state) {
        case IO_SSLWAIT:
            if(!readable && !writeable) {
                if(!iofd->ssl_server_hs) {
                    callback_event.type = IOEVENT_SSLFAILED;
                    iofd->state = IO_CLOSED;
                    engine->update(iofd);
                } else
                    iohandler_close(iofd);
            } else if(iofd->ssl_server_hs) {
                iohandler_log(IOLOG_DEBUG, "triggering iohandler_ssl_server_handshake for %s (fd: %d)", iohandler_iotype_name(iofd->type), iofd->fd);
                iohandler_ssl_server_handshake(iofd);
            } else {
                iohandler_log(IOLOG_DEBUG, "triggering iohandler_ssl_client_handshake for %s (fd: %d)", iohandler_iotype_name(iofd->type), iofd->fd);
                iohandler_ssl_client_handshake(iofd);
            }
            break;
        case IO_CLOSED:
            if(iofd->type == IOTYPE_TIMER)
                callback_event.type = IOEVENT_TIMEOUT;
            break;
        case IO_LISTENING:
            if(readable) {
                callback_event.data.accept_fd = accept(iofd->fd, NULL, 0);
                if(callback_event.data.accept_fd < 0) {
                    iohandler_log(IOLOG_ERROR, "could not accept client (server fd: %d): %d - %s", iofd->fd, errno, strerror(errno));
                } else if(iofd->ssl) {
                    struct IODescriptor *client_iofd = iohandler_add(callback_event.data.accept_fd, IOTYPE_CLIENT, NULL, NULL);
                    iohandler_ssl_client_accepted(iofd, client_iofd);
                } else
                    callback_event.type = IOEVENT_ACCEPT;
            }
            break;
        case IO_CONNECTING:
            if(readable) { //could not connect
                callback_event.type = IOEVENT_NOTCONNECTED;
                //socklen_t arglen;
                //arglen = sizeof(callback_event.data.errid);
                //if (getsockopt(iofd->fd, SOL_SOCKET, SO_ERROR, &callback_event.data.errid, &arglen) < 0)
                //    callback_event.data.errid = errno;
                iofd->state = IO_CLOSED;
				engine->update(iofd);
            } else if(writeable) {
                if(iofd->ssl && !iofd->ssl_active) {
                    iohandler_log(IOLOG_DEBUG, "triggering iohandler_ssl_connect for %s (fd: %d)", iohandler_iotype_name(iofd->type), iofd->fd);
                    iohandler_ssl_connect(iofd);
                    return;
                }
                if(iofd->ssl && iofd->ssl_server_hs)
                    callback_event.type = IOEVENT_CONNECTED;
                else {
                    callback_event.type = IOEVENT_SSLACCEPT;
                    callback_event.iofd = iofd->data;
                    callback_event.data.accept_iofd = iofd;
                    iofd->data = NULL;
                }
                iofd->state = IO_CONNECTED;
                engine->update(iofd);
            }
            break;
        case IO_CONNECTED:
            if(readable) {
                if(iofd->read_lines) {
                    int bytes;
                    
                    if(iofd->ssl_active)
                        bytes = iohandler_ssl_read(iofd, iofd->readbuf.buffer + iofd->readbuf.bufpos, iofd->readbuf.buflen - iofd->readbuf.bufpos);
                    else {
                        if(iofd->type == IOTYPE_STDIN)
                            #ifdef WIN32
                            bytes = readable;
                            #else
                            bytes = read(iofd->fd, iofd->readbuf.buffer + iofd->readbuf.bufpos, iofd->readbuf.buflen - iofd->readbuf.bufpos);
                            #endif
                        else
                            bytes = recv(iofd->fd, iofd->readbuf.buffer + iofd->readbuf.bufpos, iofd->readbuf.buflen - iofd->readbuf.bufpos, 0);
                    }
                    if(bytes <= 0) {
                        if (errno != EAGAIN || errno != EWOULDBLOCK) {
                            iofd->state = IO_CLOSED;
							engine->update(iofd);
                            callback_event.type = IOEVENT_CLOSED;
                            callback_event.data.errid = errno;
                        }
                    } else {
                        int i, used_bytes = 0;
                        iohandler_log(IOLOG_DEBUG, "received %d bytes (fd: %d). readbuf position: %d", bytes, iofd->fd, iofd->readbuf.bufpos);
                        iofd->readbuf.bufpos += bytes;
                        callback_event.type = IOEVENT_RECV;
                        for(i = 0; i < iofd->readbuf.bufpos; i++) {
                            if(iofd->readbuf.buffer[i] == '\r' && iofd->readbuf.buffer[i+1] == '\n')
                                iofd->readbuf.buffer[i] = 0;
                            else if(iofd->readbuf.buffer[i] == '\n' || iofd->readbuf.buffer[i] == '\r') {
                                iofd->readbuf.buffer[i] = 0;
                                callback_event.data.recv_str = iofd->readbuf.buffer + used_bytes;
                                iohandler_log(IOLOG_DEBUG, "parsed line (%d bytes): %s", i - used_bytes, iofd->readbuf.buffer + used_bytes);
                                used_bytes = i+1;
                                iohandler_trigger_event(&callback_event);
                            } else if(i + 1 - used_bytes >= IO_LINE_LEN) { //512 max
                                iofd->readbuf.buffer[i] = 0;
                                callback_event.data.recv_str = iofd->readbuf.buffer + used_bytes;
                                iohandler_log(IOLOG_DEBUG, "parsed and stripped line (%d bytes): %s", i - used_bytes, iofd->readbuf.buffer + used_bytes);
                                for(; i < iofd->readbuf.bufpos; i++) { //skip the rest of the line
                                    if(iofd->readbuf.buffer[i] == '\n' || (iofd->readbuf.buffer[i] == '\r' && iofd->readbuf.buffer[i+1] != '\n')) {
                                        break;
                                    }
                                }
                                used_bytes = i+1;
                                iohandler_trigger_event(&callback_event);
                            }
                        }
                        if(used_bytes) {
                            if(used_bytes == iofd->readbuf.bufpos) {
                                iofd->readbuf.bufpos = 0;
                                iohandler_log(IOLOG_DEBUG, "readbuf fully processed (set buffer position to 0)");
                            } else {
                                iohandler_log(IOLOG_DEBUG, "readbuf rest: %d bytes (used %d bytes)", iofd->readbuf.bufpos - used_bytes, used_bytes);
                                memmove(iofd->readbuf.buffer, iofd->readbuf.buffer + used_bytes, iofd->readbuf.bufpos - used_bytes);
                                iofd->readbuf.bufpos -= used_bytes;
                            }
                        }
                        callback_event.type = IOEVENT_IGNORE;
                    }
                } else
                    callback_event.type = IOEVENT_READABLE;
            }
            if(writeable) {
                int bytes;
                bytes = iohandler_try_write(iofd);
                if(bytes < 0) {
                    iofd->state = IO_CLOSED;
                    engine->update(iofd);
                    callback_event.type = IOEVENT_CLOSED;
                    callback_event.data.errid = errno;
                }
            }
            break;
    }
    if(callback_event.type == IOEVENT_IGNORE && !readable && !writeable) 
        callback_event.type = IOEVENT_TIMEOUT;
    if(callback_event.type != IOEVENT_IGNORE)
        iohandler_trigger_event(&callback_event);
}

void iohandler_poll() {
    struct timeval timeout;
    timeout.tv_sec = IO_MAX_TIMEOUT;
    timeout.tv_usec = 0;
    iohandler_poll_timeout(timeout);
}

void iohandler_poll_timeout(struct timeval timeout) {
    if(engine) {
        IOSYNCHRONIZE(io_poll_sync); //quite senceless multithread support... better support will follow
        engine->loop(&timeout);
        IODESYNCHRONIZE(io_poll_sync);
    }
}

//debugging functions
char *iohandler_iotype_name(enum IOType type) {
    switch(type) {
        case IOTYPE_UNKNOWN:
            return "IOTYPE_UNKNOWN";
        case IOTYPE_SERVER:
            return "IOTYPE_SERVER";
        case IOTYPE_CLIENT:
            return "IOTYPE_CLIENT";
        case IOTYPE_STDIN:
            return "IOTYPE_STDIN";
        case IOTYPE_TIMER:
            return "IOTYPE_TIMER";
        default:
            return "(UNDEFINED)";
    }
}

char *iohandler_iostatus_name(enum IOStatus status) {
    switch(status) {
        case IO_CLOSED:
            return "IO_CLOSED";
        case IO_LISTENING:
            return "IO_LISTENING";
        case IO_CONNECTING:
            return "IO_CONNECTING";
        case IO_CONNECTED:
            return "IO_CONNECTED";
        case IO_SSLWAIT:
            return "IO_SSLWAIT";
        default:
            return "(UNDEFINED)";
    }
}

char *iohandler_ioeventtype_name(enum IOEventType type) {
    switch(type) {
        case IOEVENT_IGNORE:
            return "IOEVENT_IGNORE";
        case IOEVENT_READABLE:
            return "IOEVENT_READABLE";
        case IOEVENT_RECV:
            return "IOEVENT_RECV";
        case IOEVENT_CONNECTED:
            return "IOEVENT_CONNECTED";
        case IOEVENT_NOTCONNECTED:
            return "IOEVENT_NOTCONNECTED";
        case IOEVENT_CLOSED:
            return "IOEVENT_CLOSED";
        case IOEVENT_ACCEPT:
            return "IOEVENT_ACCEPT";
        case IOEVENT_TIMEOUT:
            return "IOEVENT_TIMEOUT";
        default:
            return "(UNDEFINED)";
    }
}
