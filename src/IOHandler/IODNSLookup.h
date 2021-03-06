/* IODNSLookup.h - IOMultiplexer v2
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
#ifndef _IODNSLookup_h
#define _IODNSLookup_h
#include <stdlib.h>
#include "IODNSAddress.struct.h"

#ifndef _IOHandler_internals
#include "IOHandler.h"
#else

struct IODNSEngine;
extern struct IODNSEngine dnsengine_cares;
extern struct IODNSEngine dnsengine_default;

struct _IODNSQuery;
extern struct _IODNSQuery *iodnsquery_first;
extern struct _IODNSQuery *iodnsquery_last;

/* Multithreading */
#ifdef IODNS_USE_THREADS
#ifndef HAVE_PTHREAD_H
#undef IODNS_USE_THREADS
#endif
#endif
#ifdef IODNS_USE_THREADS
#include <pthread.h>
#ifdef PTHREAD_MUTEX_RECURSIVE_NP
#define PTHREAD_MUTEX_RECURSIVE_VAL PTHREAD_MUTEX_RECURSIVE_NP
#else
#define PTHREAD_MUTEX_RECURSIVE_VAL PTHREAD_MUTEX_RECURSIVE
#endif
#define IOTHREAD_MUTEX_INIT(var) { \
	pthread_mutexattr_t mutex_attr; \
	pthread_mutexattr_init(&mutex_attr);\
	pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE_VAL);\
	pthread_mutex_init(&var, &mutex_attr); \
}
#define IOSYNCHRONIZE(var) pthread_mutex_lock(&var)
#define IODESYNCHRONIZE(var) pthread_mutex_unlock(&var)
#else
#define IOTHREAD_MUTEX_INIT(var)
#define IOSYNCHRONIZE(var)
#define IODESYNCHRONIZE(var)
#endif

#define IODNSFLAG_RUNNING        0x01
#define IODNSFLAG_PROCESSING     0x02
#define IODNSFLAG_PARENT_PUBLIC  0x04
#define IODNSFLAG_PARENT_SOCKET  0x08

struct IODNSResult;
struct _IOSocket;

struct _IODNSQuery {
	void *query;
	
	unsigned int flags : 8;
	unsigned int type : 8;
	union {
		struct IODNSAddress addr;
		char *host;
	} request;
	
	struct IODNSResult *result;
	
	void *parent;
	
	struct _IODNSQuery *next, *prev;
};

struct IODNSEngine {
	const char *name;
	int (*init)();
	void (*stop)();
	void (*add)(struct _IODNSQuery *query);
	void (*remove)(struct _IODNSQuery *query);
	void (*loop)();
	void (*socket_callback)(struct _IOSocket *iosock, int readable, int writeable);
};

void _init_iodns();
void _stop_iodns();
struct _IODNSQuery *_create_dnsquery();
void _start_dnsquery(struct _IODNSQuery *query);
void _stop_dnsquery(struct _IODNSQuery *query);

/* call only from engines! */
enum IODNSEventType;
void _free_dnsquery(struct _IODNSQuery *query);
void iodns_socket_callback(struct _IOSocket *iosock, int wantread, int wantwrite);
void iodns_event_callback(struct _IODNSQuery *query, enum IODNSEventType state);
void iodns_poll();

#endif

struct IODNSEvent;
struct sockaddr;

#define IODNS_CALLBACK(NAME) void NAME(struct IODNSEvent *event)
typedef IODNS_CALLBACK(iodns_callback);

enum IODNSEventType {
	IODNSEVENT_SUCCESS,
	IODNSEVENT_FAILED
};

#define IODNS_RECORD_A    0x01
#define IODNS_RECORD_AAAA 0x02
#define IODNS_RECORD_PTR  0x04

#define IODNS_FORWARD     0x03
#define IODNS_REVERSE     0x04

struct IODNSQuery {
	void *query;
	
	iodns_callback *callback;
	void *data;
};

struct IODNSResult {
	unsigned int type : 8;
	union {
		struct IODNSAddress addr;
		char *host;
	} result;
	struct IODNSResult *next;
};

struct IODNSEvent {
	enum IODNSEventType type;
	struct IODNSQuery *query;
	struct IODNSResult *result;
};

struct IODNSQuery *iodns_getaddrinfo(char *hostname, int records, iodns_callback *callback, void *arg);
struct IODNSQuery *iodns_getnameinfo(const struct sockaddr *addr, size_t addrlen, iodns_callback *callback, void *arg);
void iodns_abort(struct IODNSQuery *query);

int iodns_print_address(struct IODNSAddress *addr, int ipv6, char *buffer, int length);

void iodns_free_result(struct IODNSResult *result);

#endif
