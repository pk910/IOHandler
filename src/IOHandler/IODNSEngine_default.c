/* IODNSEngine_default.c - IOMultiplexer
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
#include "IODNSLookup.h"

#ifdef WIN32
#define _WIN32_WINNT 0x501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif
#include "compat/inet.h"
#include <stdlib.h>
#include <string.h>


#ifdef IODNS_USE_THREADS
static pthread_t iodns_thread;
static int iodns_thread_running = 1;

static pthread_cond_t iodns_cond;
static pthread_mutex_t iodns_sync, iodns_sync2;
#endif
static int iodns_loop_blocking = 0;

static void iodns_process_queries();

#ifdef IODNS_USE_THREADS
static void *dnsengine_worker_main(void *arg) {
	struct _IODNSQuery *query;
	while(iodns_thread_running) {
		IOSYNCHRONIZE(iodns_sync);
		for(query = iodnsquery_first; query; query = query->next) {
			if((query->flags & IODNSFLAG_RUNNING))
				break;
		}
		IODESYNCHRONIZE(iodns_sync);
		if(!query)
			pthread_cond_wait(&iodns_cond, &iodns_sync2);
		
		if(iodns_thread_running)
			iodns_process_queries();
	}
	return NULL;
}
#endif

static int dnsengine_default_init() {
	#ifdef IODNS_USE_THREADS
	/* create worker thread */
	pthread_cond_init(&iodns_cond, NULL);
	IOTHREAD_MUTEX_INIT(iodns_sync);
	IOTHREAD_MUTEX_INIT(iodns_sync2);
	
	iodns_thread_running = 1;
	
	int thread_err;
	thread_err = pthread_create(&iodns_thread, NULL, dnsengine_worker_main, NULL);
	if(thread_err) {
		iolog_trigger(IOLOG_ERROR, "could not create pthread in %s:%d (Returned: %i)", __FILE__, __LINE__, thread_err);
		iodns_loop_blocking = 1;
		iodns_thread_running = 0;
	}
	#else
	iodns_loop_blocking = 1;
	#endif
    return 1;
}

static void dnsengine_default_stop() {
	#ifdef IODNS_USE_THREADS
	if(iodns_thread_running) {
		iodns_thread_running = 0;
		IOSYNCHRONIZE(iodns_sync2);
		pthread_cond_signal(&iodns_cond);
		IODESYNCHRONIZE(iodns_sync2);
		pthread_join(iodns_thread, NULL);
	}
	#endif
}

static void dnsengine_default_add(struct _IODNSQuery *iodns) {
    #ifdef IODNS_USE_THREADS
	if(iodns_thread_running) {
		IOSYNCHRONIZE(iodns_sync2);
		pthread_cond_signal(&iodns_cond);
		IODESYNCHRONIZE(iodns_sync2);
	}
	#endif
}

static void dnsengine_default_remove(struct _IODNSQuery *iodns) {
    /* unused */
}

static void dnsengine_default_loop() {
    if(iodns_loop_blocking)
		iodns_process_queries();
}

static void iodns_process_queries() {
	enum IODNSEventType querystate;
    struct addrinfo hints, *res, *allres;
    struct _IODNSQuery *iodns, *next_iodns;
    struct IODNSResult *dnsresult;
	iodns_process_queries_start:
	IOSYNCHRONIZE(iodns_sync);
    for(iodns = iodnsquery_first; iodns; iodns = next_iodns) {
        next_iodns = iodns->next;
		
		if(!(iodns->flags & IODNSFLAG_RUNNING))
			continue;
		if((iodns->flags & IODNSFLAG_PROCESSING))
			continue;
		
		IODESYNCHRONIZE(iodns_sync);
		
        querystate = IODNSEVENT_FAILED;
        
        if((iodns->type & IODNS_FORWARD)) {
            memset (&hints, 0, sizeof (hints));
            hints.ai_family = PF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags |= AI_CANONNAME;
			int ret;
            if (!(ret = getaddrinfo(iodns->request.host, NULL, &hints, &allres))) {
				res = allres;
                while (res) {
                    switch (res->ai_family) {
                    case AF_INET:
                        if((iodns->type & IODNS_RECORD_A)) {
                            dnsresult = malloc(sizeof(*dnsresult));
                            dnsresult->type = IODNS_RECORD_A;
                            dnsresult->result.addr.addresslen = res->ai_addrlen;
                            dnsresult->result.addr.address = malloc(dnsresult->result.addr.addresslen);
                            memcpy(dnsresult->result.addr.address, res->ai_addr, dnsresult->result.addr.addresslen);
                            dnsresult->next = iodns->result;
                            iodns->result = dnsresult;
                            
                            char str[INET_ADDRSTRLEN];
							inet_ntop( AF_INET, &((struct sockaddr_in *)dnsresult->result.addr.address)->sin_addr, str, INET_ADDRSTRLEN );
                            iolog_trigger(IOLOG_DEBUG, "Resolved %s to (A): %s", iodns->request.host, str);
                            
                            querystate = IODNSEVENT_SUCCESS;
                        }
                        break;
                    case AF_INET6:
                        if((iodns->type & IODNS_RECORD_AAAA)) {
                            dnsresult = malloc(sizeof(*dnsresult));
                            dnsresult->type = IODNS_RECORD_AAAA;
                            dnsresult->result.addr.addresslen = res->ai_addrlen;
                            dnsresult->result.addr.address = calloc(dnsresult->result.addr.addresslen, 1);
                            memcpy(dnsresult->result.addr.address, res->ai_addr, dnsresult->result.addr.addresslen);
                            dnsresult->next = iodns->result;
                            iodns->result = dnsresult;
                            
                            char str[INET6_ADDRSTRLEN];
							inet_ntop( AF_INET6, &((struct sockaddr_in6 *)dnsresult->result.addr.address)->sin6_addr, str, INET6_ADDRSTRLEN );
                            iolog_trigger(IOLOG_DEBUG, "Resolved %s to (AAAA): %s", iodns->request.host, str);
                            
                            querystate = IODNSEVENT_SUCCESS;
                        }
                        break;
                    }
                    res = res->ai_next;
                }
                freeaddrinfo(allres);
            } else {
				iolog_trigger(IOLOG_WARNING, "getaddrinfo returned error code: %d", ret);
			}
        }
		IOSYNCHRONIZE(iodns_sync);
		if(!(iodns->flags & IODNSFLAG_RUNNING)) {
			iodns_free_result(iodns->result);
			_free_dnsquery(iodns);
			IODESYNCHRONIZE(iodns_sync);
			goto iodns_process_queries_start;
		}
		iodns->flags &= ~(IODNSFLAG_PROCESSING | IODNSFLAG_RUNNING);
		IODESYNCHRONIZE(iodns_sync);
		iodns_event_callback(iodns, querystate);
		goto iodns_process_queries_start;
    }
}

struct IODNSEngine dnsengine_default = {
    .name = "default",
    .init = dnsengine_default_init,
	.stop = dnsengine_default_stop,
    .add = dnsengine_default_add,
    .remove = dnsengine_default_remove,
    .loop = dnsengine_default_loop,
};