/* IOSockets.c - IOMultiplexer v2
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
#include "IOSockets.h"
#include "IOLog.h"
#include "IODNSLookup.h"

#ifdef HAVE_PTHREAD_H
static pthread_mutex_t iosocket_sync, iosocket_dns_sync;
#endif

static struct _IOSocket *iosocket_first = NULL;
static struct _IOSocket *iosocket_last = NULL;

/* IO Engines */
extern struct IOEngine engine_select; /* select system call (should always be useable) */
extern struct IOEngine engine_kevent;
extern struct IOEngine engine_epoll;
extern struct IOEngine engine_win32;

struct IOEngine *engine = NULL;


static void iosocket_increase_buffer(struct IOSocketBuffer *iobuf, size_t required);
static int iosocket_parse_address(const char *hostname, struct IODNSAddress *addr, int records);
static int iosocket_lookup_hostname(struct _IOSocket *iosock, const char *hostname, int records, int bindaddr);
static void iosocket_lookup_apply(struct _IOSocket *iosock, struct IODNSAddress *addr, int bindlookup);
static void iosocket_connect_finish(struct _IOSocket *iosock);
static void iosocket_listen_finish(struct _IOSocket *iosock);




static void iosockets_init_engine() {
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
            iolog_trigger(IOLOG_FATAL, "found no useable IO engine");
            return;
        }
    }
    iolog_trigger(IOLOG_DEBUG, "using %s IOSockets engine", engine->name);
}

void _init_sockets() {
	IOTHREAD_MUTEX_INIT(iosocket_sync);
	IOTHREAD_MUTEX_INIT(iosocket_dns_sync);
	iosockets_init_engine();
}


struct _IOSocket _create_socket() {
	struct _IOSocket *iosock = calloc(1, sizeof(*iosock));
	if(!iosock) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for _IOSocket in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	IOSYNCHRONIZE(iosocket_sync);
	if(iosocket_last)
		iosocket_last->next = iosock;
	else
		iosocket_first = iosock;
	iosock->prev = iosocket_last;
	iosocket_last = iosock;
	IODESYNCHRONIZE(iosocket_sync);
	return iosock;
}

void _free_socket(struct _IOSocket *iosock) {
	iosocket_deactivate(iosock);
	IOSYNCHRONIZE(iosocket_sync);
	if(iosock->prev)
		iosock->prev->next = iosock->next;
	else
		iosocket_first = iosock->next;
	if(iosock->next)
		iosock->next->prev = iosock->prev;
	else
		iosocket_last = iosock->prev;
	IODESYNCHRONIZE(iosocket_sync);
	
	if(iosock->bind.addr.addresslen)
		free(iosock->bind.addr.address);
	if(iosock->dest.addr.addresslen)
		free(iosock->dest.addr.address);
	if(iosock->readbuf.buffer)
		free(iosock->readbuf.buffer);
	if(iosock->writebuf.buffer)
		free(iosock->writebuf.buffer);
	
	free(iosock);
}

static void iosocket_activate(struct _IOSocket *iosock) {
	if((iosock->flags & IOSOCKETFLAG_ACTIVE))
		return;
	iosock->flags |= IOSOCKETFLAG_ACTIVE;
	engine->add(iosock);
}

static void iosocket_deactivate(struct _IOSocket *iosock) {
	if(!(iosock->flags & IOSOCKETFLAG_ACTIVE))
		return;
	iosock->flags &= ~IOSOCKETFLAG_ACTIVE;
	engine->remove(iosock);
}

static void iosocket_increase_buffer(struct IOSocketBuffer *iobuf, size_t required) {
    if(iobuf->buflen >= required) return;
    char *new_buf = realloc(iobuf->buffer, required + 2);
    if(new_buf) {
        iobuf->buffer = new_buf;
        iobuf->buflen = required;
    }
}

static int iosocket_parse_address(const char *hostname, struct IODNSAddress *addr, int records) {
	int ret;
	if((flags & IOSOCKET_ADDR_IPV4)) {
		struct sockaddr_in ip4addr;
		ret = inet_pton(AF_INET, hostname, &(ip4addr.sin_addr));
		if(ret == 1) {
			addr->addresslen = sizeof(*ip4addr);
			addr->address = malloc(addr->addresslen);
			if(!addr->address) {
				iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__);
				return -1;
			}
			memcpy(addr->address, ip4addr, addr->addresslen);
			return 1;
		}
	}
	if((flags & IOSOCKET_ADDR_IPV6)) {
		struct sockaddr_in6 ip6addr;
		ret = inet_pton(AF_INET6, hostname, &(ip6addr.sin6_addr));
		if(ret == 1) {
			addr->addresslen = sizeof(*ip6addr);
			addr->address = malloc(addr->addresslen);
			if(!addr->address) {
				iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__);
				return -1;
			}
			memcpy(addr->address, ip6addr, addr->addresslen);
			return 1;
		}
	}
	return 0;
}

static int iosocket_lookup_hostname(struct _IOSocket *iosock, const char *hostname, int records, int bindaddr) {
	struct IOSocketDNSLookup *lookup = calloc(1, sizeof(*lookup));
	if(!lookup) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOSocketDNSLookup in %s:%d", __FILE__, __LINE__);
		return 0;
	}
	
	struct _IODNSQuery *query = _create_dnsquery();
	if(!query) {
		free(lookup);
		return 0;
	}
	
	query->parent = lookup;
	query->flags |= IODNSFLAG_PARENT_SOCKET;
	lookup->iosocket = iosocket;
	lookup->query = query;
	strncpy(lookup->hostname, hostname, sizeof(lookup->hostname));
	lookup->hostname[sizeof(lookup->hostname)-1] = 0;
	if(bindaddr) {
		lookup->bindlookup = 1;
		iosock->bind.addrlookup = lookup;
	} else {
		lookup->bindlookup = 0;
		iosock->dest.addrlookup = lookup;
	}
	
	int dnsrecords = 0;
	if((records & IOSOCKET_ADDR_IPV4))
		dnsrecords |= IODNS_RECORD_A;
	if((records & IOSOCKET_ADDR_IPV6))
		dnsrecords |= IODNS_RECORD_AAAA;
	
	query->request.host = strdup(hostname);
	query->type = (dnsrecords & IODNS_FORWARD);
	
	_start_dnsquery(query);
	return 1;
}

void iosocket_lookup_callback(struct IOSocketDNSLookup *lookup, struct IODNSEvent *event) {
	lookup->query = NULL;
	IOSYNCHRONIZE(iosocket_dns_sync);
	struct _IOSocket *iosock = lookup->iosock;
	if(iosock == NULL) {
		IODESYNCHRONIZE(iosocket_dns_sync);
		return;
	}
	if(event->type == IODNSEVENT_SUCCESS)
		lookup->result = event->result;
	else
		lookup->result = NULL;
	
	if(lookup->bindlookup) {
		iosock->flags &= ~IOSOCKETFLAG_PENDING_BINDDNS;
		iosock->flags |= IOSOCKETFLAG_DNSDONE_BINDDNS;
	} else {
		iosock->flags &= ~IOSOCKETFLAG_PENDING_DESTDNS;
		iosock->flags |= IOSOCKETFLAG_DNSDONE_DESTDNS;
	}
	
	int dns_finished = 0;
	if((iosock->flags & (IOSOCKETFLAG_PENDING_BINDDNS | IOSOCKETFLAG_PENDING_DESTDNS)) == 0)
		dns_finished = 1;
	IODESYNCHRONIZE(iosocket_dns_sync);
	if(dns_finished) {
		int ret;
		ret = iosocket_lookup_apply(iosock);
		if(ret) { //if ret=0 an error occured in iosocket_lookup_apply and we should stop here.
			if((iosock->flags & IOSOCKETFLAG_LISTENING))
				iosocket_listen_finish(iosock);
			else
				iosocket_connect_finish(iosock);
		}
	}
}

static void iosocket_lookup_apply(struct _IOSocket *iosock) {
	char errbuf[512];
	struct IOSocketDNSLookup *bind_lookup = ((iosock->flags & IOSOCKETFLAG_DNSDONE_BINDDNS) ? iosock->bind.addrlookup : NULL);
	struct IOSocketDNSLookup *dest_lookup = ((iosock->flags & IOSOCKETFLAG_DNSDONE_DESTDNS) ? iosock->dest.addrlookup : NULL);
	if(!bind_lookup && !dest_lookup) {
		iosock->flags |= IOSOCKETFLAG_DNSERROR;
		sprintf(errbuf, "Internal Error");
		iolog_trigger(IOLOG_ERROR, "trying to apply lookup results without any lookups processed in %s:%d", __FILE__, __LINE__);
		goto iosocket_lookup_clear;
	}
	
	struct IODNSResult *result;
	int bind_numip4 = 0, bind_numip6 = 0;
	int dest_numip4 = 0, dest_numip6 = 0;
	
	if(bind_lookup) {
		for(result = bind_lookup->result; result; result = result->next) {
			if((result->type & IODNS_RECORD_A))
				bind_numip4++;
			if((result->type & IODNS_RECORD_AAAA))
				bind_numip6++;
		}
	}
	if(dest_lookup) {
		for(result = dest_lookup->result; result; result = result->next) {
			if((result->type & IODNS_RECORD_A))
				dest_numip4++;
			if((result->type & IODNS_RECORD_AAAA))
				dest_numip6++;
		}
	}
	int useip6 = 0;
	int useip4 = 0;
	if(bind_lookup && (bind_numip6 == 0 && bind_numip4 == 0)) {
		iosock->flags |= IOSOCKETFLAG_DNSERROR;
		sprintf(errbuf, "could not lookup bind address (%s)", bind_lookup->hostname);
		goto iosocket_lookup_clear;
	} else if(dest_lookup && (dest_numip6 == 0 && dest_numip4 == 0)) {
		iosock->flags |= IOSOCKETFLAG_DNSERROR;
		sprintf(errbuf, "could not lookup destination address (%s)", dest_lookup->hostname);
		goto iosocket_lookup_clear;
	} else if(bind_lookup && dest_lookup) {
		if(bind_numip6 > 0 && dest_numip6 > 0)
			useip6 = 1;
		else if(bind_numip4 > 0 && dest_numip4 > 0)
			useip4 = 1;
		else {
			iosock->flags |= IOSOCKETFLAG_DNSERROR;
			sprintf(errbuf, "could not lookup adresses of the same IP family for bind and destination host. (bind: %d ip4, %d ip6 | dest: %d ip4, %d ip6)", bind_numip4, bind_numip6, dest_numip4, dest_numip6);
			goto iosocket_lookup_clear;
		}
	} else if(bind_lookup) {
		if(bind_numip6)
			useip6 = 1;
		else if(bind_numip4)
			useip4 = 1;
	} else if(dest_lookup) {
		if(dest_numip6)
			useip6 = 1;
		else if(dest_numip4)
			useip4 = 1;
	}
	
	int usetype = 0;
	if(useip6) {
		usetype = IODNS_RECORD_AAAA;
		iosock->flags |= IOSOCKETFLAG_IPV6SOCKET;
	} else {
		usetype = IODNS_RECORD_A;
	}
	
	#define IOSOCKET_APPLY_COPYADDR(type) \
	iosock->type.addr.addresslen = result->result.addr.addresslen; \
	iosock->type.addr.address = malloc(result->result.addr.addresslen); \
	if(!iosock->type.addr.address) { \
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__); \
		iosock->type.addr.addresslen = 0; \
		iosock->flags |= IOSOCKETFLAG_DNSERROR; \
		sprintf(errbuf, "could not allocate memory for dns information"); \
		goto iosocket_lookup_clear; \
	} \
	memcpy(iosock->type.addr.address, result->result.addr.address, result->result.addr.addresslen);
	
	
	if(bind_lookup) {
		int usenum = (useip6 ? bind_numip6 : bind_numip4);
		usenum = rand() % usenum;
		for(result = bind_lookup->result; result; result = result->next) {
			if((result->type & usetype)) {
				if(usenum == 0) {
					IOSOCKET_APPLY_COPYADDR(bind)
					break;
				}
				usenum--;
			}
		}
	} else
		iosock->bind.addr.addresslen = 0;
	
	if(dest_lookup) {
		int usenum = (useip6 ? dest_numip6 : dest_numip4);
		usenum = rand() % usenum;
		for(result = dest_lookup->result; result; result = result->next) {
			if((result->type & usetype)) {
				if(usenum == 0) {
					IOSOCKET_APPLY_COPYADDR(dest)
					break;
				}
				usenum--;
			}
		}
	} else
		iosock->dest.addr.addresslen = 0;
	
	iosocket_lookup_clear:
	if(bind_lookup) {
		if(bind_lookup->result)
			iodns_free_result(bind_lookup->result);
		free(bind_lookup);
	}
	if(dest_lookup) {
		if(bind_lookup->result)
			iodns_free_result(dest_lookup->result);
		free(dest_lookup);
	}
	
	if((iosock->flags & IOSOCKETFLAG_DNSERROR)) {
		// TODO: trigger error
		
		return 0;
	} else
		return 1;
}

static void iosocket_connect_finish(struct _IOSocket *iosock) {
	int sockfd;
	if((iosock->flags & IOSOCKETFLAG_IPV6SOCKET))
		sockfd = socket(AF_INET6, SOCK_STREAM, 0);
	else
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1) {
		iolog_trigger(IOLOG_ERROR, "could not create socket in %s:%d", __FILE__, __LINE__);
		// TODO: trigger error
		
		return;
	}
	
	// set port and bind address
	if((iosock->flags & IOSOCKETFLAG_IPV6SOCKET)) {
		struct sockaddr_in6 *ip6 = iosock->dest.addr.address;
		ip6->sin6_family = AF_INET6;
        ip6->sin6_port = htons(iosock->port);
		
		if(iosock->bind.addr.addresslen) {
			struct sockaddr_in6 *ip6bind = iosock->bind.addr.address;
			ip6bind->sin6_family = AF_INET6;
			ip6bind->sin6_port = htons(0);
			
			bind(sockfd, (struct sockaddr*)ip6bind, sizeof(*ip6bind));
		}
	} else {
		struct sockaddr_in *ip4 = iosock->dest.addr.address;
		ip->sin_family = AF_INET;
        ip->sin_port = htons(iosock->port);
		
		if(iosock->bind.addr.addresslen) {
			struct sockaddr_in *ip4bind = iosock->bind.addr.address;
			ip4bind->sin_family = AF_INET;
			ip4bind->sin6_port = htons(0);
			
			bind(sockfd, (struct sockaddr*)ip4bind, sizeof(*ip4bind));
		}
	}
	
	// prevent SIGPIPE
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
	
    // make sockfd unblocking
    #if defined(F_GETFL)
    {
        int fcntl_flags;
        fcntl_flags = fcntl(sockfd, F_GETFL);
        fcntl(sockfd, F_SETFL, fcntl_flags|O_NONBLOCK);
        fcntl_flags = fcntl(sockfd, F_GETFD);
        fcntl(sockfd, F_SETFD, fcntl_flags|FD_CLOEXEC);
    }
    #else
    /* I hope you're using the Win32 backend or something else that
     * automatically marks the file descriptor non-blocking...
     */
    #endif
	
	connect(sockfd, iosock->dest.addr.address, iosock->dest.addr.addresslen); //returns EINPROGRESS here (nonblocking)
	iosock->fd = sockfd;
	
	iosocket_activate(iosock);
}

static void iosocket_listen_finish(struct _IOSocket *iosock) {
	int sockfd;
	if((iosock->flags & IOSOCKETFLAG_IPV6SOCKET))
		sockfd = socket(AF_INET6, SOCK_STREAM, 0);
	else
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1) {
		iolog_trigger(IOLOG_ERROR, "could not create socket in %s:%d", __FILE__, __LINE__);
		// TODO: trigger error
		
		return;
	}
	
	// set port and bind address
	if((iosock->flags & IOSOCKETFLAG_IPV6SOCKET)) {
		struct sockaddr_in6 *ip6bind = iosock->bind.addr.address;
		ip6bind->sin6_family = AF_INET6;
		ip6bind->sin6_port = htons(0);
		
		int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
		
		bind(sockfd, (struct sockaddr*)ip6bind, sizeof(*ip6bind));
	} else {
		struct sockaddr_in *ip4bind = iosock->bind.addr.address;
		ip4bind->sin_family = AF_INET;
		ip4bind->sin6_port = htons(0);
		
		int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
		
		bind(sockfd, (struct sockaddr*)ip4bind, sizeof(*ip4bind));
	}
	
	// prevent SIGPIPE
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
	
    // make sockfd unblocking
    #if defined(F_GETFL)
    {
        int fcntl_flags;
        fcntl_flags = fcntl(sockfd, F_GETFL);
        fcntl(sockfd, F_SETFL, fcntl_flags|O_NONBLOCK);
        fcntl_flags = fcntl(sockfd, F_GETFD);
        fcntl(sockfd, F_SETFD, fcntl_flags|FD_CLOEXEC);
    }
    #else
    /* I hope you're using the Win32 backend or something else that
     * automatically marks the file descriptor non-blocking...
     */
    #endif
	
	listen(sockfd, 1);
	iosock->fd = sockfd;
	
	iosocket_activate(iosock);
}

/* public functions */

struct IOSocket *iosocket_connect(const char *hostname, unsigned int port, int ssl, const char *bindhost, iosocket_callback *callback) {
	return iosocket_connect_flags(hostname, port, ssl, bindhost, callback, (IOSOCKET_ADDR_IPV4 | IOSOCKET_ADDR_IPV6));
}

struct IOSocket *iosocket_connect_flags(const char *hostname, unsigned int port, int ssl, const char *bindhost, iosocket_callback *callback, int flags) {
	struct IOSocket *iodescriptor = calloc(1, sizeof(*iodescriptor));
	if(!iodescriptor) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOSocket in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	
	struct _IOSocket *iosock = _create_socket();
	if(!iosock) {
		free(iodescriptor);
		return NULL;
	}
	
	iodescriptor->iosocket = iosock;
	iodescriptor->status = IOSOCKET_CONNECTING;
	iodescriptor->callback = callback;
	iosock->parent = iodescriptor;
	iosock->flags |= IOSOCKETFLAG_PARENT_PUBLIC;
	if(ssl)
		iosock->flags |= IOSOCKETFLAG_SSLSOCKET;
	
	IOSYNCHRONIZE(iosocket_dns_sync);
	if(bindhost) {
		switch(iosocket_parse_address(bindhost, &iosock->bind.addr, flags)) {
		case -1:
			free(iosock);
			return NULL;
		case 0:
			/* start dns lookup */
			iosock->flags |= IOSOCKETFLAG_PENDING_BINDDNS;
			iosocket_lookup_hostname(iosock, bindhost, flags, 1);
			break;
		case 1:
			/* valid address */
			break;
		}
	}
	switch(iosocket_parse_address(hostname, &iosock->dest.addr, flags)) {
	case -1:
		free(iosock);
		return NULL;
	case 0:
		/* start dns lookup */
		iosock->flags |= IOSOCKETFLAG_PENDING_DESTDNS;
		iosocket_lookup_hostname(iosock, hostname, flags, 0);
		break;
	case 1:
		/* valid address */
		break;
	}
	IODESYNCHRONIZE(iosocket_dns_sync);
	if((iosock->flags & (IOSOCKETFLAG_PENDING_BINDDNS | IOSOCKETFLAG_PENDING_DESTDNS)) == 0) {
		iosocket_connect_finish(iosock);
	}
	return iodescriptor;
}

struct IOSocket *iosocket_listen(const char *hostname, unsigned int port, iosocket_callback *callback) {
	return iosocket_listen_flags(hostname, port, callback, (IOSOCKET_ADDR_IPV4 | IOSOCKET_ADDR_IPV6));
}

struct IOSocket *iosocket_listen_flags(const char *hostname, unsigned int port, iosocket_callback *callback, int flags) {
	struct IOSocket *iodescriptor = calloc(1, sizeof(*iodescriptor));
	if(!iodescriptor) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOSocket in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	
	struct _IOSocket *iosock = _create_socket();
	if(!iosock) {
		free(iodescriptor);
		return NULL;
	}
	
	iodescriptor->iosocket = iosock;
	iodescriptor->status = IOSOCKET_CONNECTING;
	iodescriptor->callback = callback;
	iosock->parent = iodescriptor;
	iosock->flags |= IOSOCKETFLAG_PARENT_PUBLIC | IOSOCKETFLAG_LISTENING;
	if(ssl)
		iosock->flags |= IOSOCKETFLAG_SSLSOCKET;
	
	IOSYNCHRONIZE(iosocket_dns_sync);
	switch(iosocket_parse_address(hostname, &iosock->bind.addr, flags)) {
	case -1:
		free(iosock);
		return NULL;
	case 0:
		/* start dns lookup */
		iosock->flags |= IOSOCKETFLAG_PENDING_BINDDNS;
		iosocket_lookup_hostname(iosock, hostname, flags, 1);
		break;
	case 1:
		/* valid address */
		break;
	}
	IODESYNCHRONIZE(iosocket_dns_sync);
	if((iosock->flags & IOSOCKETFLAG_PENDING_BINDDNS) == 0) {
		iosocket_listen_finish(iosock);
	}
	return iodescriptor;
}

struct IOSocket *iosocket_listen_ssl(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iosocket_callback *callback) {
	return iosocket_listen_ssl_flags(hostname, port, certfile, keyfile, callback, (IOSOCKET_ADDR_IPV4 | IOSOCKET_ADDR_IPV6));
}

struct IOSocket *iosocket_listen_ssl_flags(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iosocket_callback *callback, int flags) {
	//TODO: SSL
	return NULL;
}

void iosocket_close(struct IOSocket *iosocket) {
	struct _IOSocket *iosock = iosocket->iosocket;
	if(iosock == NULL) {
		iolog_trigger(IOLOG_WARNING, "called iosocket_close for destroyed IOSocket in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	
	iosock->flags |= IOSOCKETFLAG_SHUTDOWN;
	
	if(iosock->writebuf.bufpos) {
        //try to send everything before closing
#if defined(F_GETFL)
        {
            int flags;
            flags = fcntl(iosock->fd, F_GETFL);
            fcntl(iosock->fd, F_SETFL, flags & ~O_NONBLOCK);
            flags = fcntl(iosock->fd, F_GETFD);
            fcntl(iosock->fd, F_SETFD, flags|FD_CLOEXEC);
        }
#else
        iosocket_deactivate(iosock);
#endif
        iosocket_try_write(iosock);
    }
	//close IOSocket
    if(iosock->sslnode) {
		//TODO: SSL
	}
	close(iosock->fd);
    _free_socket(iosock);
	iosocket->iosocket = NULL;
	iosocket->status = IOSOCKET_CLOSED;
	iogc_add(iosocket);
}

static int iohandler_try_write(struct _IOSocket *iosock) {
    if(!iosock->writebuf.bufpos) return 0;
    iolog_trigger(IOLOG_DEBUG, "write writebuf (%d bytes) to socket (fd: %d)", iosock->writebuf.bufpos, iosock->fd);
    int res;
    if(iosock->sslnode) {
        /* res = iohandler_ssl_write(iofd, iofd->writebuf.buffer, iofd->writebuf.bufpos); */
		// TODO
    } else
        res = send(iosock->fd, iosock->writebuf.buffer, iosock->writebuf.bufpos, 0);
    if(res < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            iolog_trigger(IOLOG_ERROR, "could not write to socket (fd: %d): %d - %s", iosock->fd, errno, strerror(errno));
        else
            res = 0;
    } else {
        iosock->writebuf.bufpos -= res;
		if((iosock->flags & (IOSOCKETFLAG_ACTIVE & IOSOCKETFLAG_SHUTDOWN)) == IOSOCKETFLAG_ACTIVE)
			engine->update(iosock);
    }
    return res;
}

void iosocket_send(struct IOSocket *iosocket, const char *data, size_t datalen) {
	struct _IOSocket *iosock = iosocket->iosocket;
	if(iosock == NULL) {
		iolog_trigger(IOLOG_WARNING, "called iosocket_close for destroyed IOSocket in %s:%d", __FILE__, __LINE__);
		return;
	}
	if(iosock->flags & IOSOCKETFLAG_SHUTDOWN) {
        iolog_trigger(IOLOG_ERROR, "could not write to socket (socket is closing)");
        return;
    }
    iolog_trigger(IOLOG_DEBUG, "add %d to writebuf (fd: %d): %s", datalen, iosock->fd, data);
    if(iosock->writebuf.buflen < iosock->writebuf.bufpos + datalen) {
        iolog_trigger(IOLOG_DEBUG, "increase writebuf (curr: %d) to %d (+%d bytes)", iosock->writebuf.buflen, iosock->writebuf.bufpos + datalen, (iosock->writebuf.bufpos + datalen - iosock->writebuf.buflen));
        iosocket_increase_buffer(&iosock->writebuf, iosock->writebuf.bufpos + datalen);
        if(iosock->writebuf.buflen < iosock->writebuf.bufpos + datalen) {
            iolog_trigger(IOLOG_ERROR, "increase writebuf (curr: %d) to %d (+%d bytes) FAILED", iosock->writebuf.buflen, iosock->writebuf.bufpos + datalen, (iosock->writebuf.bufpos + datalen - iosock->writebuf.buflen));
            return;
        }
    }
    memcpy(iosock->writebuf.buffer + iosock->writebuf.bufpos, data, datalen);
    iosock->writebuf.bufpos += datalen;
	if((iosock->flags & IOSOCKETFLAG_ACTIVE))
		engine->update(iosock);
}

void iosocket_write(struct IOSocket *iosocket, const char *line) {
	size_t linelen = strlen(line);
    iohandler_send(iosocket, line, linelen);
}

void iosocket_printf(struct IOSocket *iosocket, const char *text, ...) {
	va_list arg_list;
    char sendBuf[IOSOCKET_LINE_LEN];
    int pos;
    sendBuf[0] = '\0';
    va_start(arg_list, text);
    pos = vsnprintf(sendBuf, IOSOCKET_LINE_LEN - 2, text, arg_list);
    va_end(arg_list);
    if (pos < 0 || pos > (IOSOCKET_LINE_LEN - 2)) pos = IOSOCKET_LINE_LEN - 2;
    sendBuf[pos] = '\n';
    sendBuf[pos+1] = '\0';
    iohandler_send(iosocket, sendBuf, pos+1);
}





