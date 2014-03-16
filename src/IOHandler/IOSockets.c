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
#include "IOSSLBackend.h"

#ifdef WIN32
#define _WIN32_WINNT 0x501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#include "compat/inet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

struct _IOSocket *iosocket_first = NULL;
struct _IOSocket *iosocket_last = NULL;

struct IOEngine *engine = NULL;

static void iosocket_increase_buffer(struct IOSocketBuffer *iobuf, size_t required);
static int iosocket_parse_address(const char *hostname, struct IODNSAddress *addr, int records);
static int iosocket_lookup_hostname(struct _IOSocket *iosock, const char *hostname, int records, int bindaddr);
static int iosocket_lookup_apply(struct _IOSocket *iosock, int noip6);
static void socket_lookup_clear(struct _IOSocket *iosock);
static void iosocket_connect_finish(struct _IOSocket *iosock);
static void iosocket_listen_finish(struct _IOSocket *iosock);
static int iosocket_try_write(struct _IOSocket *iosock);
static void iosocket_trigger_event(struct IOSocketEvent *event);

#ifdef WIN32
static int close(int fd) {
	return closesocket(fd);
}
#endif

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
	#ifdef WIN32
	WSADATA wsaData;
    int iResult;
	//Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if(iResult != 0){
        iolog_trigger(IOLOG_ERROR, "WSAStartup returned error code: %d", iResult);
    }
	#endif
	
	iosockets_init_engine();
	iossl_init();
}


struct _IOSocket *_create_socket() {
	struct _IOSocket *iosock = calloc(1, sizeof(*iosock));
	if(!iosock) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for _IOSocket in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	if(iosocket_last)
		iosocket_last->next = iosock;
	else
		iosocket_first = iosock;
	iosock->prev = iosocket_last;
	iosocket_last = iosock;
	return iosock;
}

void _free_socket(struct _IOSocket *iosock) {
	iosocket_deactivate(iosock);
	if(iosock->prev)
		iosock->prev->next = iosock->next;
	else
		iosocket_first = iosock->next;
	if(iosock->next)
		iosock->next->prev = iosock->prev;
	else
		iosocket_last = iosock->prev;
	
	if(iosock->bind.addr.addresslen)
		free(iosock->bind.addr.address);
	if(iosock->dest.addr.addresslen)
		free(iosock->dest.addr.address);
	if(iosock->bind.addrlookup || iosock->dest.addrlookup)
		socket_lookup_clear(iosock);
	if(iosock->readbuf.buffer)
		free(iosock->readbuf.buffer);
	if(iosock->writebuf.buffer)
		free(iosock->writebuf.buffer);
	if((iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET))
		iossl_disconnect(iosock);
	
	free(iosock);
}

void iosocket_activate(struct _IOSocket *iosock) {
	if((iosock->socket_flags & IOSOCKETFLAG_ACTIVE))
		return;
	iosock->socket_flags |= IOSOCKETFLAG_ACTIVE;
	engine->add(iosock);
}

void iosocket_deactivate(struct _IOSocket *iosock) {
	if(!(iosock->socket_flags & IOSOCKETFLAG_ACTIVE))
		return;
	iosock->socket_flags &= ~IOSOCKETFLAG_ACTIVE;
	engine->remove(iosock);
}

void iosocket_update(struct _IOSocket *iosock) {
	if(!(iosock->socket_flags & IOSOCKETFLAG_ACTIVE))
		return;
	engine->update(iosock);
}

static void iosocket_increase_buffer(struct IOSocketBuffer *iobuf, size_t required) {
	if(iobuf->buflen >= required) return;
	char *new_buf;
	if(iobuf->buffer)
		new_buf = realloc(iobuf->buffer, required + 2);
	else
		new_buf = malloc(required + 2);
	if(new_buf) {
		iobuf->buffer = new_buf;
		iobuf->buflen = required;
	}
}

static int iosocket_parse_address(const char *hostname, struct IODNSAddress *addr, int records) {
	int ret;
	if((records & IOSOCKET_ADDR_IPV4)) {
		struct sockaddr_in ip4addr;
		ret = inet_pton(AF_INET, hostname, &(ip4addr.sin_addr));
		if(ret == 1) {
			addr->addresslen = sizeof(ip4addr);
			addr->address = malloc(addr->addresslen);
			if(!addr->address) {
				iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__);
				return -1;
			}
			memcpy(addr->address, &ip4addr, sizeof(ip4addr));
			return 1;
		}
	}
	if((records & IOSOCKET_ADDR_IPV6)) {
		struct sockaddr_in6 ip6addr;
		ret = inet_pton(AF_INET6, hostname, &(ip6addr.sin6_addr));
		if(ret == 1) {
			addr->addresslen = sizeof(ip6addr);
			addr->address = malloc(addr->addresslen);
			if(!addr->address) {
				iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__);
				return -1;
			}
			memcpy(addr->address, &ip6addr, sizeof(ip6addr));
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
	lookup->iosocket = iosock;
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
	struct _IOSocket *iosock = lookup->iosocket;
	if(iosock == NULL)
		return;
	
	if(event->type == IODNSEVENT_SUCCESS)
		lookup->result = event->result;
	else
		lookup->result = NULL;
	
	if(lookup->bindlookup) {
		iosock->socket_flags &= ~IOSOCKETFLAG_PENDING_BINDDNS;
		iosock->socket_flags |= IOSOCKETFLAG_DNSDONE_BINDDNS;
	} else {
		iosock->socket_flags &= ~IOSOCKETFLAG_PENDING_DESTDNS;
		iosock->socket_flags |= IOSOCKETFLAG_DNSDONE_DESTDNS;
	}
	
	int dns_finished = 0;
	if((iosock->socket_flags & (IOSOCKETFLAG_PENDING_BINDDNS | IOSOCKETFLAG_PENDING_DESTDNS)) == 0)
		dns_finished = 1;
	
	if(dns_finished) {
		int ret;
		ret = iosocket_lookup_apply(iosock, 0);
		if(ret) { //if ret=0 an error occured in iosocket_lookup_apply and we should stop here.
			if((iosock->socket_flags & IOSOCKETFLAG_LISTENING)) {
				socket_lookup_clear(iosock);
				iosocket_listen_finish(iosock);
			} else
				iosocket_connect_finish(iosock);
		}
	}
}

static int iosocket_lookup_apply(struct _IOSocket *iosock, int noip6) {
	char errbuf[512];
	struct IOSocketDNSLookup *bind_lookup = ((iosock->socket_flags & IOSOCKETFLAG_DNSDONE_BINDDNS) ? iosock->bind.addrlookup : NULL);
	struct IOSocketDNSLookup *dest_lookup = ((iosock->socket_flags & IOSOCKETFLAG_DNSDONE_DESTDNS) ? iosock->dest.addrlookup : NULL);
	
	iolog_trigger(IOLOG_DEBUG, "all pending lookups finished. trying to apply lookup results...");
	
	if(!bind_lookup && !dest_lookup) {
		iosock->socket_flags |= IOSOCKETFLAG_DNSERROR;
		sprintf(errbuf, "Internal Error");
		iolog_trigger(IOLOG_ERROR, "trying to apply lookup results without any lookups processed in %s:%d", __FILE__, __LINE__);
		goto iosocket_lookup_apply_end;
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
		iosock->socket_flags |= IOSOCKETFLAG_DNSERROR;
		sprintf(errbuf, "could not lookup bind address (%s)", bind_lookup->hostname);
		goto iosocket_lookup_apply_end;
	} else if(dest_lookup && (dest_numip6 == 0 && dest_numip4 == 0)) {
		iosock->socket_flags |= IOSOCKETFLAG_DNSERROR;
		sprintf(errbuf, "could not lookup destination address (%s)", dest_lookup->hostname);
		goto iosocket_lookup_apply_end;
	} else if(bind_lookup && dest_lookup) {
		if(bind_numip6 > 0 && dest_numip6 > 0)
			useip6 = 1;
		if(bind_numip4 > 0 && dest_numip4 > 0)
			useip4 = 1;
	} else if(bind_lookup) {
		if(bind_numip6)
			useip6 = 1;
		if(bind_numip4)
			useip4 = 1;
	} else if(dest_lookup) {
		if(dest_numip6)
			useip6 = 1;
		if(dest_numip4)
			useip4 = 1;
	}
	
	int usetype = 0;
	if(useip6 && !noip6) {
		usetype = IODNS_RECORD_AAAA;
		iosock->socket_flags |= IOSOCKETFLAG_IPV6SOCKET;
		if(useip4)
			iosock->socket_flags |= IOSOCKETFLAG_RECONNECT_IPV4;
	} else if(useip4) {
		usetype = IODNS_RECORD_A;
		iosock->socket_flags &= ~(IOSOCKETFLAG_IPV6SOCKET | IOSOCKETFLAG_RECONNECT_IPV4);
	} else {
		iosock->socket_flags |= IOSOCKETFLAG_DNSERROR;
		sprintf(errbuf, "could not lookup adresses of the same IP family for bind and destination host. (bind: %d ip4, %d ip6 | dest: %d ip4, %d ip6)", bind_numip4, bind_numip6, dest_numip4, dest_numip6);
		goto iosocket_lookup_apply_end;
	}
	
	#define IOSOCKET_APPLY_COPYADDR(type) \
	iosock->type.addr.addresslen = result->result.addr.addresslen; \
	iosock->type.addr.address = malloc(result->result.addr.addresslen); \
	if(!iosock->type.addr.address) { \
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__); \
		iosock->type.addr.addresslen = 0; \
		iosock->socket_flags |= IOSOCKETFLAG_DNSERROR; \
		sprintf(errbuf, "could not allocate memory for dns information"); \
		goto iosocket_lookup_apply_end; \
	} \
	memcpy(iosock->type.addr.address, result->result.addr.address, result->result.addr.addresslen);
	
	
	if(bind_lookup) {
		int usenum = ((usetype == IODNS_RECORD_AAAA) ? bind_numip6 : bind_numip4);
		usenum = rand() % usenum;
		for(result = bind_lookup->result; result; result = result->next) {
			if((result->type & usetype)) {
				if(usenum == 0) {
					inet_ntop(((usetype == IODNS_RECORD_AAAA) ? AF_INET6 : AF_INET), ((usetype == IODNS_RECORD_AAAA) ? (void *)(&((struct sockaddr_in6 *)result->result.addr.address)->sin6_addr) : (void *)(&((struct sockaddr_in *)result->result.addr.address)->sin_addr)), errbuf, sizeof(errbuf));
					iolog_trigger(IOLOG_DEBUG, "using IPv%s Address (%s) as bind address", ((usetype == IODNS_RECORD_AAAA) ? "6" : "4"), errbuf);
					IOSOCKET_APPLY_COPYADDR(bind)
					break;
				}
				usenum--;
			}
		}
	} else
		iosock->bind.addr.addresslen = 0;
	
	if(dest_lookup) {
		int usenum = ((usetype == IODNS_RECORD_AAAA) ? dest_numip6 : dest_numip4);
		usenum = rand() % usenum;
		for(result = dest_lookup->result; result; result = result->next) {
			if((result->type & usetype)) {
				if(usenum == 0) {
					inet_ntop(((usetype == IODNS_RECORD_AAAA) ? AF_INET6 : AF_INET), ((usetype == IODNS_RECORD_AAAA) ? (void *)(&((struct sockaddr_in6 *)result->result.addr.address)->sin6_addr) : (void *)(&((struct sockaddr_in *)result->result.addr.address)->sin_addr)), errbuf, sizeof(errbuf));
					iolog_trigger(IOLOG_DEBUG, "using IPv%s Address (%s) as dest address", ((usetype == IODNS_RECORD_AAAA) ? "6" : "4"), errbuf);
					IOSOCKET_APPLY_COPYADDR(dest)
					break;
				}
				usenum--;
			}
		}
	} else
		iosock->dest.addr.addresslen = 0;
	
	iosocket_lookup_apply_end:
	
	if((iosock->socket_flags & IOSOCKETFLAG_DNSERROR)) {
		// TODO: trigger error
		iolog_trigger(IOLOG_ERROR, "error while trying to apply dns lookup information: %s", errbuf);
		
		if((iosock->socket_flags & IOSOCKETFLAG_PARENT_PUBLIC)) {
			//trigger event
			struct IOSocket *iosocket = iosock->parent;
			
			struct IOSocketEvent callback_event;
			callback_event.type = IOSOCKETEVENT_DNSFAILED;
			callback_event.socket = iosocket;
			callback_event.data.recv_str = errbuf;
			iosocket_trigger_event(&callback_event);
			
			iosocket_close(iosocket);
		} else {
			// TODO: IODNS Callback
		}
		return 0;
	} else
		return 1;
}

static void socket_lookup_clear(struct _IOSocket *iosock) {
	struct IOSocketDNSLookup *bind_lookup = ((iosock->socket_flags & IOSOCKETFLAG_DNSDONE_BINDDNS) ? iosock->bind.addrlookup : NULL);
	struct IOSocketDNSLookup *dest_lookup = ((iosock->socket_flags & IOSOCKETFLAG_DNSDONE_DESTDNS) ? iosock->dest.addrlookup : NULL);
	if(bind_lookup) {
		if(bind_lookup->result)
			iodns_free_result(bind_lookup->result);
		free(bind_lookup);
		iosock->bind.addrlookup = NULL;
	}
	if(dest_lookup) {
		if(dest_lookup->result)
			iodns_free_result(dest_lookup->result);
		free(dest_lookup);
		iosock->dest.addrlookup = NULL;
	}
}

static void iosocket_prepare_fd(int sockfd) {
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
	#elif defined(FIONBIO)
	{
		unsigned long ulong = 1;
		ioctlsocket(sockfd, FIONBIO, &ulong);
	}
	#endif
}

static void iosocket_connect_finish(struct _IOSocket *iosock) {
	int sockfd;
	if((iosock->socket_flags & IOSOCKETFLAG_IPV6SOCKET))
		sockfd = socket(AF_INET6, SOCK_STREAM, 0);
	else
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1) {
		iolog_trigger(IOLOG_ERROR, "could not create socket in %s:%d", __FILE__, __LINE__);
		// TODO: trigger error
		
		return;
	}
	
	// set port and bind address
	if((iosock->socket_flags & IOSOCKETFLAG_IPV6SOCKET)) {
		struct sockaddr_in6 *ip6 = (void*) iosock->dest.addr.address;
		ip6->sin6_family = AF_INET6;
		ip6->sin6_port = htons(iosock->port);
		
		if(iosock->bind.addr.addresslen) {
			struct sockaddr_in6 *ip6bind = (void*) iosock->bind.addr.address;
			ip6bind->sin6_family = AF_INET6;
			ip6bind->sin6_port = htons(0);
			
			bind(sockfd, (struct sockaddr*)ip6bind, sizeof(*ip6bind));
		}
	} else {
		struct sockaddr_in *ip4 = (void*) iosock->dest.addr.address;
		ip4->sin_family = AF_INET;
		ip4->sin_port = htons(iosock->port);
		
		if(iosock->bind.addr.addresslen) {
			struct sockaddr_in *ip4bind = (void*) iosock->bind.addr.address;
			ip4bind->sin_family = AF_INET;
			ip4bind->sin_port = htons(0);
			
			bind(sockfd, (struct sockaddr*)ip4bind, sizeof(*ip4bind));
		}
	}
	
	iosocket_prepare_fd(sockfd);
	
	int ret = connect(sockfd, iosock->dest.addr.address, iosock->dest.addr.addresslen); //returns EINPROGRESS here (nonblocking)
	iolog_trigger(IOLOG_DEBUG, "connecting socket (connect: %d)", ret);
	
	iosock->fd = sockfd;
	iosock->socket_flags |= IOSOCKETFLAG_CONNECTING;
	
	iosocket_activate(iosock);
}

static void iosocket_listen_finish(struct _IOSocket *iosock) {
	int sockfd;
	if((iosock->socket_flags & IOSOCKETFLAG_IPV6SOCKET))
		sockfd = socket(AF_INET6, SOCK_STREAM, 0);
	else
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1) {
		iolog_trigger(IOLOG_ERROR, "could not create socket in %s:%d", __FILE__, __LINE__);
		// TODO: trigger error
		
		return;
	}
	
	// set port and bind address
	if((iosock->socket_flags & IOSOCKETFLAG_IPV6SOCKET)) {
		struct sockaddr_in6 *ip6bind = (void*) iosock->bind.addr.address;
		ip6bind->sin6_family = AF_INET6;
		ip6bind->sin6_port = htons(iosock->port);
		
		int opt = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
		
		bind(sockfd, (struct sockaddr*)ip6bind, sizeof(*ip6bind));
	} else {
		struct sockaddr_in *ip4bind = (void*) iosock->bind.addr.address;
		ip4bind->sin_family = AF_INET;
		ip4bind->sin_port = htons(iosock->port);
		
		int opt = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
		
		bind(sockfd, (struct sockaddr*)ip4bind, sizeof(*ip4bind));
	}
	
	iosocket_prepare_fd(sockfd);
	
	listen(sockfd, 1);
	iosock->fd = sockfd;
	
	iosocket_activate(iosock);
}

struct _IOSocket *iosocket_accept_client(struct _IOSocket *iosock) {
	struct IOSocket *new_iosocket = calloc(1, sizeof(*new_iosocket));
	if(!new_iosocket) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOSocket in %s:%d", __FILE__, __LINE__);
		close(accept(iosock->fd, NULL, 0)); // simply drop connection
		return NULL;
	}
	struct _IOSocket *new_iosock = _create_socket();
	if(!new_iosock) {
		free(new_iosocket);
		close(accept(iosock->fd, NULL, 0)); // simply drop connection
		return NULL;
	}
	new_iosocket->iosocket = new_iosock;
	new_iosocket->status = IOSOCKET_CONNECTED;
	new_iosocket->data = iosock;
	new_iosock->parent = new_iosocket;
	new_iosock->socket_flags |= IOSOCKETFLAG_PARENT_PUBLIC | IOSOCKETFLAG_INCOMING | (iosock->socket_flags & IOSOCKETFLAG_IPV6SOCKET);
	
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	
	//accept client
	new_iosock->fd = accept(iosock->fd, (struct sockaddr *)&addr, &addrlen);
	
	//copy remote address
	new_iosock->dest.addr.address = malloc(addrlen);
	if(!new_iosock->dest.addr.address) {
		close(new_iosock->fd);
		free(new_iosock);
		free(new_iosock);
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	memcpy(new_iosock->dest.addr.address, &addr, addrlen);
	new_iosock->dest.addr.addresslen = addrlen;
	
	//copy local address
	new_iosock->bind.addr.address = malloc(iosock->bind.addr.addresslen);
	if(!new_iosock->bind.addr.address) {
		close(new_iosock->fd);
		free(new_iosock);
		free(new_iosock);
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	memcpy(new_iosock->bind.addr.address, iosock->bind.addr.address, iosock->bind.addr.addresslen);
	new_iosock->bind.addr.addresslen = iosock->bind.addr.addresslen;
	
	//prepare new socket fd
	iosocket_prepare_fd(new_iosock->fd);
	
	if((iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET)) {
		new_iosocket->ssl = 1;
		new_iosock->socket_flags |= IOSOCKETFLAG_SSLSOCKET;
		
		iossl_client_accepted(iosock, new_iosock);
	}
	
	iosocket_activate(new_iosock);
	return new_iosock;
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
	iosock->socket_flags |= IOSOCKETFLAG_PARENT_PUBLIC;
	iosock->port = port;
	if(ssl) {
		iodescriptor->ssl = 1;
		iosock->socket_flags |= IOSOCKETFLAG_SSLSOCKET;
	}
	
	if(bindhost) {
		switch(iosocket_parse_address(bindhost, &iosock->bind.addr, flags)) {
		case -1:
			free(iosock);
			return NULL;
		case 0:
			/* start dns lookup */
			iosock->socket_flags |= IOSOCKETFLAG_PENDING_BINDDNS;
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
		iosock->socket_flags |= IOSOCKETFLAG_PENDING_DESTDNS;
		iosocket_lookup_hostname(iosock, hostname, flags, 0);
		break;
	case 1:
		/* valid address */
		break;
	}
	if((iosock->socket_flags & (IOSOCKETFLAG_PENDING_BINDDNS | IOSOCKETFLAG_PENDING_DESTDNS)) == 0) {
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
	iodescriptor->status = IOSOCKET_LISTENING;
	iodescriptor->listening = 1;
	iodescriptor->callback = callback;
	iosock->parent = iodescriptor;
	iosock->socket_flags |= IOSOCKETFLAG_PARENT_PUBLIC | IOSOCKETFLAG_LISTENING;
	iosock->port = port;
	
	switch(iosocket_parse_address(hostname, &iosock->bind.addr, flags)) {
	case -1:
		free(iosock);
		return NULL;
	case 0:
		/* start dns lookup */
		iosock->socket_flags |= IOSOCKETFLAG_PENDING_BINDDNS;
		iosocket_lookup_hostname(iosock, hostname, flags, 1);
		break;
	case 1:
		/* valid address */
		break;
	}
	if((iosock->socket_flags & IOSOCKETFLAG_PENDING_BINDDNS) == 0) {
		iosocket_listen_finish(iosock);
	}
	return iodescriptor;
}

struct IOSocket *iosocket_listen_ssl(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iosocket_callback *callback) {
	return iosocket_listen_ssl_flags(hostname, port, certfile, keyfile, callback, (IOSOCKET_ADDR_IPV4 | IOSOCKET_ADDR_IPV6));
}

struct IOSocket *iosocket_listen_ssl_flags(const char *hostname, unsigned int port, const char *certfile, const char *keyfile, iosocket_callback *callback, int flags) {
	struct IOSocket *iosocket = iosocket_listen_flags(hostname, port, callback, flags);
	struct _IOSocket *iosock = iosocket->iosocket;
	iosock->socket_flags |= IOSOCKETFLAG_SSLSOCKET;
	iossl_listen(iosock, certfile, keyfile);
	return iosocket;
}

void iosocket_close(struct IOSocket *iosocket) {
	struct _IOSocket *iosock = iosocket->iosocket;
	if(iosock == NULL) {
		iolog_trigger(IOLOG_WARNING, "called iosocket_close for destroyed IOSocket in %s:%d", __FILE__, __LINE__);
		return;
	}
	
	iosock->socket_flags |= IOSOCKETFLAG_SHUTDOWN;
	
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
	if((iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET))
		iossl_disconnect(iosock);
	if(iosock->fd)
		close(iosock->fd);
	_free_socket(iosock);
	iosocket->iosocket = NULL;
	iosocket->status = IOSOCKET_CLOSED;
	iogc_add(iosocket);
}

static int iosocket_try_write(struct _IOSocket *iosock) {
	if(!iosock->writebuf.bufpos && !(iosock->socket_flags & IOSOCKETFLAG_SSL_WRITEHS)) 
		return 0;
	iolog_trigger(IOLOG_DEBUG, "write writebuf (%d bytes) to socket (fd: %d)", iosock->writebuf.bufpos, iosock->fd);
	int res;
	if((iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET))
		res = iossl_write(iosock, iosock->writebuf.buffer, iosock->writebuf.bufpos);
	else
		res = send(iosock->fd, iosock->writebuf.buffer, iosock->writebuf.bufpos, 0);
	if(res < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			iolog_trigger(IOLOG_ERROR, "could not write to socket (fd: %d): %d - %s", iosock->fd, errno, strerror(errno));
		else
			res = 0;
	} else {
		iosock->writebuf.bufpos -= res;
		if((iosock->socket_flags & (IOSOCKETFLAG_ACTIVE | IOSOCKETFLAG_SHUTDOWN)) == IOSOCKETFLAG_ACTIVE)
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
	if(iosock->socket_flags & IOSOCKETFLAG_SHUTDOWN) {
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
	if((iosock->socket_flags & IOSOCKETFLAG_ACTIVE))
		engine->update(iosock);
}

void iosocket_write(struct IOSocket *iosocket, const char *line) {
	size_t linelen = strlen(line);
	iosocket_send(iosocket, line, linelen);
}

void iosocket_printf(struct IOSocket *iosocket, const char *text, ...) {
	va_list arg_list;
	char sendBuf[IOSOCKET_PRINTF_LINE_LEN];
	int pos;
	sendBuf[0] = '\0';
	va_start(arg_list, text);
	pos = vsnprintf(sendBuf, IOSOCKET_PRINTF_LINE_LEN - 1, text, arg_list);
	va_end(arg_list);
	if (pos < 0 || pos > (IOSOCKET_PRINTF_LINE_LEN - 1)) pos = IOSOCKET_PRINTF_LINE_LEN - 1;
	sendBuf[pos] = '\0';
	iosocket_send(iosocket, sendBuf, pos);
}


int iosocket_wants_reads(struct _IOSocket *iosock) {
	if((iosock->socket_flags & (IOSOCKETFLAG_SSL_READHS | IOSOCKETFLAG_SSL_WRITEHS)))
		return ((iosock->socket_flags & IOSOCKETFLAG_SSL_WANTWRITE) ? 0 : 1);
	if(!(iosock->socket_flags & IOSOCKETFLAG_OVERRIDE_WANT_RW))
		return 1;
	else if((iosock->socket_flags & IOSOCKETFLAG_OVERRIDE_WANT_R))
		return 1;
	return 0;
}
int iosocket_wants_writes(struct _IOSocket *iosock) {
	if((iosock->socket_flags & (IOSOCKETFLAG_SSL_READHS | IOSOCKETFLAG_SSL_WRITEHS)))
		return ((iosock->socket_flags & IOSOCKETFLAG_SSL_WANTWRITE) ? 1 : 0);
	if(!(iosock->socket_flags & IOSOCKETFLAG_OVERRIDE_WANT_RW)) {
		if(iosock->writebuf.bufpos || (iosock->socket_flags & IOSOCKETFLAG_CONNECTING))
			return 1;
		else
			return 0;
	} else if((iosock->socket_flags & IOSOCKETFLAG_OVERRIDE_WANT_W))
		return 1;
	return 0;
}


static void iosocket_trigger_event(struct IOSocketEvent *event) {
	if(!event->socket->callback) 
		return;
	iolog_trigger(IOLOG_DEBUG, "triggering event");
	event->socket->callback(event);
}

void iosocket_events_callback(struct _IOSocket *iosock, int readable, int writeable) {
	if((iosock->socket_flags & IOSOCKETFLAG_PARENT_PUBLIC)) {
		struct IOSocket *iosocket = iosock->parent;
		struct IOSocketEvent callback_event;
		callback_event.type = IOSOCKETEVENT_IGNORE;
		callback_event.socket = iosocket;
		
		if((iosock->socket_flags & IOSOCKETFLAG_SSL_HANDSHAKE)) {
			if(readable || writeable) {
				if((iosock->socket_flags & IOSOCKETFLAG_INCOMING)) 
					iossl_server_handshake(iosock);
				else
					iossl_client_handshake(iosock);
				engine->update(iosock);
			} else if((iosock->socket_flags & IOSOCKETFLAG_LISTENING)) {
				//TODO: SSL init error
			} else if((iosock->socket_flags & IOSOCKETFLAG_INCOMING)) {
				if((iosock->socket_flags & IOSOCKETFLAG_SSL_ESTABLISHED)) {
					//incoming SSL connection accepted
					iosock->socket_flags &= ~IOSOCKETFLAG_SSL_HANDSHAKE;
					callback_event.type = IOSOCKETEVENT_ACCEPT;
					callback_event.data.accept_socket = iosock->parent;
					struct _IOSocket *parent_socket = iosocket->data;
					callback_event.socket = parent_socket->parent;
					
					//initialize readbuf
					iosocket_increase_buffer(&iosock->readbuf, 1024);
				} else {
					//incoming SSL connection failed, simply drop
					iosock->socket_flags |= IOSOCKETFLAG_DEAD;
					iolog_trigger(IOLOG_ERROR, "SSL Handshake failed for incoming connection. Dropping fd %d", iosock->fd);
				}
			} else {
				// SSL Backend finished
				if((iosock->socket_flags & IOSOCKETFLAG_SSL_ESTABLISHED)) {
					iosocket->status = IOSOCKET_CONNECTED;
					iosock->socket_flags &= ~IOSOCKETFLAG_SSL_HANDSHAKE;
					callback_event.type = IOSOCKETEVENT_CONNECTED;
					engine->update(iosock);
					
					//initialize readbuf
					iosocket_increase_buffer(&iosock->readbuf, 1024);
				} else {
					callback_event.type = IOSOCKETEVENT_NOTCONNECTED;
					iosock->socket_flags |= IOSOCKETFLAG_DEAD;
				}
			}
		} else if((iosock->socket_flags & IOSOCKETFLAG_LISTENING)) {
			if(readable) {
				//new client connected
				struct _IOSocket *new_iosock = iosocket_accept_client(iosock);
				if(!new_iosock)
					return;
				
				if(!(new_iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET)) {
					callback_event.type = IOSOCKETEVENT_ACCEPT;
					callback_event.data.accept_socket = new_iosock->parent;
				}
			}
			
		} else if((iosock->socket_flags & IOSOCKETFLAG_CONNECTING)) {
			if(readable) { //could not connect
				if((iosock->socket_flags & (IOSOCKETFLAG_IPV6SOCKET | IOSOCKETFLAG_RECONNECT_IPV4)) == (IOSOCKETFLAG_IPV6SOCKET | IOSOCKETFLAG_RECONNECT_IPV4)) {
					iolog_trigger(IOLOG_DEBUG, "connecting to IPv6 host (%s) failed. trying to connect using IPv4.", iosock->dest.addrlookup->hostname);
					iosocket_deactivate(iosock);
					if(iosocket_lookup_apply(iosock, 1)) { //if ret=0 an error occured in iosocket_lookup_apply and we should stop here.
						iosocket_connect_finish(iosock);
						socket_lookup_clear(iosock);
					}
				} else {
					callback_event.type = IOSOCKETEVENT_NOTCONNECTED;
					/*
					socklen_t arglen = sizeof(callback_event.data.errid);
					if (getsockopt(iosock->fd, SOL_SOCKET, SO_ERROR, &callback_event.data.errid, &arglen) < 0)
						callback_event.data.errid = errno;
					*/
					iosock->socket_flags |= IOSOCKETFLAG_DEAD;
				}
			} else if(writeable) { //connection established
				iosock->socket_flags &= ~IOSOCKETFLAG_CONNECTING;
				socket_lookup_clear(iosock);
				if((iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET)) {
					iolog_trigger(IOLOG_DEBUG, "SSL client socket connected. Stating SSL handshake...");
					iossl_connect(iosock);
					engine->update(iosock);
					return;
				}
				iosocket->status = IOSOCKET_CONNECTED;
				
				callback_event.type = IOSOCKETEVENT_CONNECTED;
				engine->update(iosock);
				
				//initialize readbuf
				iosocket_increase_buffer(&iosock->readbuf, 1024);
			}
		} else {
			int ssl_rehandshake = 0;
			if((iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET)) {
				if((iosock->socket_flags & IOSOCKETFLAG_SSL_READHS))
					ssl_rehandshake = 1;
				else if((iosock->socket_flags & IOSOCKETFLAG_SSL_WRITEHS))
					ssl_rehandshake = 2;
			}
			iosocketevents_callback_retry_read:
			if((readable && ssl_rehandshake == 0) || ssl_rehandshake == 1) {
				int bytes;
				if(iosock->readbuf.buflen - iosock->readbuf.bufpos >= 128) {
					int addsize;
					if(iosock->readbuf.buflen >= 2048)
						addsize = 1024;
					else
						addsize = iosock->readbuf.buflen;
					iosocket_increase_buffer(&iosock->readbuf, iosock->readbuf.buflen + addsize);
				}
				if((iosock->socket_flags & IOSOCKETFLAG_SSLSOCKET))
					bytes = iossl_read(iosock, iosock->readbuf.buffer + iosock->readbuf.bufpos, iosock->readbuf.buflen - iosock->readbuf.bufpos);
				else 
					bytes = recv(iosock->fd, iosock->readbuf.buffer + iosock->readbuf.bufpos, iosock->readbuf.buflen - iosock->readbuf.bufpos, 0);
				
				if(bytes <= 0) {
					if((iosock->socket_flags & (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_READHS)) == (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_READHS)) {
						ssl_rehandshake = 1;
					} else if (errno != EAGAIN || errno != EWOULDBLOCK) {
						iosock->socket_flags |= IOSOCKETFLAG_DEAD;
						
						callback_event.type = IOSOCKETEVENT_CLOSED;
						callback_event.data.errid = errno;
					}
				} else {
					int i;
					iolog_trigger(IOLOG_DEBUG, "received %d bytes (fd: %d). readbuf position: %d", bytes, iosock->fd, iosock->readbuf.bufpos);
					iosock->readbuf.bufpos += bytes;
					int retry_read = (iosock->readbuf.bufpos == iosock->readbuf.buflen);
					callback_event.type = IOSOCKETEVENT_RECV;
					
					if(iosocket->parse_delimiter) {
						int j, used_bytes = 0;
						for(i = 0; i < iosock->readbuf.bufpos; i++) {
							int is_delimiter = 0;
							for(j = 0; j < IOSOCKET_PARSE_DELIMITERS_COUNT; j++) {
								if(iosock->readbuf.buffer[i] == iosocket->delimiters[j]) {
									is_delimiter = 1;
									break;
								}
							}
							if(is_delimiter) {
								iosock->readbuf.buffer[i] = 0;
								callback_event.data.recv_str = iosock->readbuf.buffer + used_bytes;
								iolog_trigger(IOLOG_DEBUG, "parsed line (%d bytes): %s", i - used_bytes, iosock->readbuf.buffer + used_bytes);
								used_bytes = i+1;
								if(iosock->readbuf.buffer[i-1] != 0 || iosocket->parse_empty)
									iosocket_trigger_event(&callback_event);
							}
							#ifdef IOSOCKET_PARSE_LINE_LIMIT
							else if(i + 1 - used_bytes >= IOSOCKET_PARSE_LINE_LIMIT) {
								iosock->readbuf.buffer[i] = 0;
								callback_event.data.recv_str = iosock->readbuf.buffer + used_bytes;
								iolog_trigger(IOLOG_DEBUG, "parsed and stripped line (%d bytes): %s", i - used_bytes, iosock->readbuf.buffer + used_bytes);
								for(; i < iosock->readbuf.bufpos; i++) { //skip the rest of the line
									is_delimiter = 0;
									if(iosock->readbuf.buffer[i] == iosocket->delimiters[j])
										break;
								}
								used_bytes = i+1;
								iosocket_trigger_event(&callback_event);
							}
							#endif
						}
						if(used_bytes) {
							if(used_bytes == iosock->readbuf.bufpos) {
								iosock->readbuf.bufpos = 0;
								iolog_trigger(IOLOG_DEBUG, "readbuf fully processed (set buffer position to 0)");
							} else {
								iolog_trigger(IOLOG_DEBUG, "readbuf rest: %d bytes (used %d bytes)", iosock->readbuf.bufpos - used_bytes, used_bytes);
								memmove(iosock->readbuf.buffer, iosock->readbuf.buffer + used_bytes, iosock->readbuf.bufpos - used_bytes);
								iosock->readbuf.bufpos -= used_bytes;
							}
						}
						callback_event.type = IOSOCKETEVENT_IGNORE;
					} else
						callback_event.data.recv_buf = &iosock->readbuf;
					if(retry_read)
						goto iosocketevents_callback_retry_read;
				}
			}
			if((writeable && ssl_rehandshake == 0) || ssl_rehandshake == 2) {
				int bytes;
				bytes = iosocket_try_write(iosock);
				if(bytes < 0) {
					if((iosock->socket_flags & (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_WRITEHS)) == (IOSOCKETFLAG_SSLSOCKET | IOSOCKETFLAG_SSL_WRITEHS)) {
						ssl_rehandshake = 1;
					} else {
						iosock->socket_flags |= IOSOCKETFLAG_DEAD;
						
						callback_event.type = IOSOCKETEVENT_CLOSED;
						callback_event.data.errid = errno;
					}
				}
			}
			if(ssl_rehandshake) {
				engine->update(iosock);
			}
		}
		if(callback_event.type != IOSOCKETEVENT_IGNORE)
			iosocket_trigger_event(&callback_event);
		if((iosock->socket_flags & IOSOCKETFLAG_DEAD))
			iosocket_close(iosocket);
		
	} else if((iosock->socket_flags & IOSOCKETFLAG_PARENT_DNSENGINE)) {
		iodns_socket_callback(iosock, readable, writeable);
	}
}

void iosocket_loop(int usec) {
	struct timeval timeout;
	timeout.tv_sec = usec / 1000000;
	timeout.tv_usec = usec % 1000000;
	engine->loop(&timeout);
}
