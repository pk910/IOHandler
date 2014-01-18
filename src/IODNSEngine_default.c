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
#include "IODNSHandler.h"

#ifdef HAVE_PTHREAD_H
static pthread_t *iodns_thread;
static int iodns_thread_running = 1;

static pthread_cond_t iodns_cond;
static pthread_mutex_t iodns_sync2;
#endif
static int iodns_loop_blocking = 0;

static void iodns_process_queries();

static void dnsengine_worker_main(void *arg) {
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
}

static int dnsengine_default_init() {
	#ifdef HAVE_PTHREAD_H
	/* create worker thread */
	pthread_cond_init(&iodns_cond, NULL);
	IOTHREAD_MUTEX_INIT(iodns_sync2);
	
	iodns_thread_running = 1;
	
	int thread_err;
	thread_err = pthread_create(&iodns_thread, NULL, dnsengine_worker_main, NULL);
	if(thread_err) {
		iolog_trigger(IOLOG_ERROR, "could not create pthread in %s:%d (Returned: %i)", __FILE__, __LINE__, thread_err);
		iodns_loop_blocking = 1;
		iodns_thread = NULL;
		iodns_thread_running = 0;
	}
	#else
	iodns_loop_blocking = 1;
	#endif
    return 1;
}

static void dnsengine_default_stop() {
	#ifdef HAVE_PTHREAD_H
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
    #ifdef HAVE_PTHREAD_H
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
    struct addrinfo hints, *res, *next_res;
    struct _IODNSQuery *iodns, *next_iodns;
    struct IODNSResult *dnsresult;
	iodns_process_queries_start:
	IOSYNCHRONIZE(iodns_sync);
    for(iodns = first_dnsquery; iodns; iodns = next_iodns) {
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
            if (!getaddrinfo(iodns->request.host, NULL, &hints, &res)) {
                while (res) {
                    switch (res->ai_family) {
                    case AF_INET:
                        if((iodns->type & IODNS_RECORD_A)) {
                            dnsresult = malloc(sizeof(*dnsresult));
                            dnsresult->type = IODNS_RECORD_A;
                            dnsresult->result.addr.addresslen = res->ai_addrlen;
                            dnsresult->result.addr.address = malloc(dnsresult->addresslen);
                            memcpy(dnsresult->address, res->ai_addr, dnsresult->addresslen);
                            dnsresult->next = iodns->result;
                            iodns->result = dnsresult;
                            querystate = IODNSEVENT_SUCCESS;
                        }
                        break;
                    case AF_INET6:
                        if((iodns->type & IODNS_RECORD_AAAA)) {
                            dnsresult = malloc(sizeof(*dnsresult));
                            dnsresult->type = IODNS_RECORD_AAAA;
                            dnsresult->result.addr.addresslen = res->ai_addrlen;
                            dnsresult->result.addr.address = malloc(dnsresult->addresslen);
                            memcpy(dnsresult->address, res->ai_addr, dnsresult->addresslen);
                            dnsresult->next = iodns->result;
                            iodns->result = dnsresult;
                            querystate = IODNSEVENT_SUCCESS;
                        }
                        break;
                    }
                    next_res = res->ai_next;
                    freeaddrinfo(res);
                    res = next_res;
                }
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
