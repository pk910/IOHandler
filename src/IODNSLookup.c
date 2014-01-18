/* IODNSLookup.c - IOMultiplexer
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
#include "IODNSLookup.h"
#include "IOLog.h"
#include "IOSockets.h"

#ifdef HAVE_PTHREAD_H
pthread_mutex_t iodns_sync;
#endif

struct _IODNSQuery *iodnsquery_first = NULL;
struct _IODNSQuery *iodnsquery_last = NULL;

struct IODNSEngine *dnsengine = NULL;

static void iodns_init_engine() {
	if(dnsengine)
		return;
	//try DNS engines
	if(dnsengine_cares.init && dnsengine_cares.init())
		dnsengine = &dnsengine_cares;
	else if(dnsengine_default.init && dnsengine_default.init())
		dnsengine = &dnsengine_default;
	else {
		iohandler_log(IOLOG_FATAL, "found no useable IO DNS engine");
		return;
	}
	iohandler_log(IOLOG_DEBUG, "using %s IODNS engine", dnsengine->name);
}

void _init_iodns() {
	IOTHREAD_MUTEX_INIT(iodns_sync);
	iodns_init_engine();
}

struct _IODNSQuery *_create_dnsquery() {
	struct _IODNSQuery *query = calloc(1, sizeof(*query));
	if(!query) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for _IODNSQuery in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	IOSYNCHRONIZE(iodns_sync);
	if(iodnsquery_last)
		iodnsquery_last->next = query;
	else
		iodnsquery_first = query;
	query->prev = iodnsquery_last;
	iodnsquery_last = query;
	IODESYNCHRONIZE(iodns_sync);
	return query;
}

void _start_dnsquery(struct _IODNSQuery *query) {
	IOSYNCHRONIZE(iodns_sync);
	query->flags |= IODNSFLAG_RUNNING;
	dnsengine->add(query);
	IODESYNCHRONIZE(iodns_sync);
}

void _free_dnsquery(struct _IODNSQuery *query) {
	IOSYNCHRONIZE(iodns_sync);
	if(query->prev)
		query->prev->next = query->next;
	else
		iodnsquery_first = query->next;
	if(query->next)
		query->next->prev = query->prev;
	else
		iodnsquery_last = query->prev;
	IODESYNCHRONIZE(iodns_sync);
	if((query->type & IODNS_REVERSE) && query->request.addr.address)
		free(query->request.addr.address);
	free(query);
}

void _stop_dnsquery(struct _IODNSQuery *query) {
	IOSYNCHRONIZE(iodns_sync);
	if((query->flags & IODNSFLAG_RUNNING)) {
		query->flags &= ~IODNSFLAG_RUNNING;
		dnsengine->remove(query);
	}
	if(!(query->flags & IODNSFLAG_PROCESSING))
		_free_dnsquery(query);
	IODESYNCHRONIZE(iodns_sync);
}

void iodns_event_callback(struct _IODNSQuery *query, enum IODNSEventType state) {
	if((query->flags & IODNSFLAG_PARENT_PUBLIC)) {
		struct IODNSQuery *descriptor = query->parent;
		struct IODNSEvent event;
		event.type = state;
		event.query = descriptor;
		event.result = query->result;
		
		descriptor->parent = NULL;
		_stop_dnsquery(query);
		
		if(descriptor->callback)
			descriptor->callback(&event);
		
		iogc_add(descriptor);
	} else if((query->flags & IODNSFLAG_PARENT_SOCKET)) {
		struct IODNSEvent event;
		event.type = state;
		event.query = NULL;
		event.result = query->result
		void *parent = query->parent;
		
		_stop_dnsquery(query);
		iosocket_lookup_callback(parent, &event);
		
	}
}

void iodns_poll() {
	if(dnsengine)
		dnsengine.loop();
}

/* public functions */

struct IODNSQuery *iodns_getaddrinfo(char *hostname, int records, iodns_callback *callback) {
	if(!(records & IODNS_FORWARD) || !hostname || !callback)
		return NULL;
	
	struct IODNSQuery *descriptor = calloc(1, sizeof(*descriptor));
	if(!descriptor) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for IODNSQuery in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	
	struct _IODNSQuery *query = _create_dnsquery();
	if(!query) {
		free(descriptor);
		return NULL
	}
	
	query->parent = descriptor;
	query->flags |= IODNSFLAG_PARENT_PUBLIC;
	descriptor->query = query;
	
	query->request.host = strdup(hostname);
	query->type = (records & IODNS_FORWARD);
	
	descriptor->callback = callback;
	
	_start_dnsquery(query);
	return descriptor;
}

struct IODNSQuery *iodns_getnameinfo(const struct sockaddr *addr, socklen_t addrlen, iodns_callback *callback) {
	if(!addr || !callback)
		return NULL;
	
	struct IODNSQuery *descriptor = calloc(1, sizeof(*descriptor));
	if(!descriptor) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for IODNSQuery in %s:%d", __FILE__, __LINE__);
		return NULL;
	}
	
	struct _IODNSQuery *query = _create_dnsquery();
	if(!query) {
		free(descriptor);
		return NULL
	}
	
	query->parent = descriptor;
	query->flags |= IODNSFLAG_PARENT_PUBLIC;
	descriptor->query = query;
	
	query->type = IODNS_RECORD_PTR;
	query->request.addr.addresslen = addrlen;
	query->request.addr.address = malloc(addrlen);
	if(!query->request.addr.address) {
		iolog_trigger(IOLOG_ERROR, "could not allocate memory for sockaddr in %s:%d", __FILE__, __LINE__);
		_free_dnsquery(query);
		free(descriptor);
		return NULL;
	}
	memcpy(query->request.addr.address, addr, addrlen);
	
	descriptor->callback = callback;
	
	_start_dnsquery(query);
	return descriptor;
}

void iodns_abort(struct IODNSQuery *descriptor) {
	if(!descriptor)
		return;
	
	struct _IODNSQuery *query = descriptor->query;
	if(!query) {
		iolog_trigger(IOLOG_WARNING, "called iodns_abort for destroyed IODNSQuery in %s:%d", __FILE__, __LINE__);
		return;
	}
	
	_stop_dnsquery(query)
}

void iodns_free_result(struct IODNSResult *result) {
	struct IODNSResult *next;
	for(;result;result = next) {
		next = result->next;
		
		if((result->type & IODNS_FORWARD)) {
			if(result->result.addr.address)
				free(result->result.addr.address);
		}
		if((result->type & IODNS_REVERSE)) {
			if(result->result.host)
				free(result->result.host);
		}
		free(result);
	}
}

