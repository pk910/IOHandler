/* IOHandler.c - IOMultiplexer v2
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
#include "IOGarbageCollector.h"
#include "IOTimer.h"
#include "IODNSLookup.h"

/* compat */
#include "compat/usleep.c"

#ifdef HAVE_PTHREAD_H
static pthread_mutex_t iothread_sync;
#ifdef WIN32
#define pthread_self_tid() pthread_self().p
#else
#define pthread_self_tid() pthread_self()
#endif

#endif

static struct IOHandlerThread {
	unsigned int id;
	unsigned int main : 1;
	unsigned int run : 1;
	unsigned int shutdown : 1;
	#ifdef HAVE_PTHREAD_H
	static pthread_t *thread;
	#endif
	struct IOHandlerThread *next;
}

static int iohandler_running = 0;
static int iohandler_treads = 1;
static struct IOHandlerThread *threads;

void iohandler_init() {
	IOTHREAD_MUTEX_INIT(iothread_sync);
	
	iolog_init();
	iogc_init();
	
	_init_timers();
	_init_iodns();
	_init_sockets();
	
	iohandler_running = 1;
}

void iohandler_set_threads(int threadcount) {
	iohandler_treads = threadcount;
}

void iohandler_stop() {
	iohandler_treads = 0;
}

#ifdef HAVE_PTHREAD_H
static void iohandler_start_worker() {
	struct IOHandlerThread *thread = calloc(1, sizeof(*thread));
    if(!thread) {
        iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOHandlerThread in %s:%d", __FILE__, __LINE__);
        return;
    }
	struct IOHandlerThread *cthread;
	for(cthread = threads; cthread; cthread = cthread->next) {
		if(cthread->next == NULL) {
			cthread->next = thread;
			break;
		}
	}
	
	thread->run = 1;
	
	int thread_err;
	thread_err = pthread_create(&thread->thread, NULL, iohandler_worker, thread);
	if(thread_err) {
		cthread->next = NULL;
		iolog_trigger(IOLOG_ERROR, "could not create pthread in %s:%d (Returned: %i)", __FILE__, __LINE__, thread_err);
	}
}
#endif

static void iohandler_worker(void *tptr) {
	struct IOHandlerThread *thread = tptr;
	
	#ifdef HAVE_PTHREAD_H
	if(!thread->main) {
		thread->id = pthread_self_tid();
	}
	#endif
	
	while(!thread->shutdown) { // endless loop
		if(thread->main && iohandler_treads != iohandler_running) {
			IOSYNCHRONIZE(iothread_sync);
			#ifdef HAVE_PTHREAD_H
			int i;
			if(iohandler_treads > iohandler_running) {
				for(i = 0; i < (iohandler_treads - iohandler_running); i++)
					iohandler_start_worker();
			}
			if(iohandler_treads < iohandler_running) {
				struct IOHandlerThread *cthread;
				for(i = 0; i < (iohandler_running - iohandler_treads); i++) {
					for(cthread = threads; cthread; cthread = cthread->next) {
						if(cthread->main)
							continue;
						cthread->shutdown = 1;
						iolog_trigger(IOLOG_ERROR, "Thread %d marked for shutdown.", cthread->id);
					}
					if(cthread)
						iohandler_running--;
				}
			}
			#endif
			if(iohandler_treads == 0) {
				#ifdef HAVE_PTHREAD_H
				struct IOHandlerThread *cthread;
				for(cthread = threads; cthread; cthread = cthread->next) {
					if(cthread->main)
						continue;
					cthread->shutdown = 1;
					pthread_join(cthread->thread, NULL);
				}
				#endif
				thread->shutdown = 1;
				IODESYNCHRONIZE(iothread_sync);
				break;
			}
			IODESYNCHRONIZE(iothread_sync);
		}
		if(!thread->run) {
			usleep(500000); // 500ms
			continue;
		}
		
		// iohandler calls
		iogc_exec();
		iodns_poll();
		
	}
	IOSYNCHRONIZE(iothread_sync);
	if(thread == threads) {
		threads = thread->next;
	} else {
		struct IOHandlerThread *cthread;
		for(cthread = threads; cthread; cthread = cthread->next) {
			if(cthread->next == thread) {
				cthread->next = thread->next;
				break;
			}
		}
	}
	iolog_trigger(IOLOG_DEBUG, "Thread %d stopped.", thread->id);
	free(thread);
	IODESYNCHRONIZE(iothread_sync);
}

void iohandler_run() {
	if(!iohandler_running)
		return;
	iohandler_running = 1;
	
	struct IOHandlerThread *mainthread = calloc(1, sizeof(*mainthread));
    if(!mainthread) {
        iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOHandlerThread in %s:%d", __FILE__, __LINE__);
        return;
    }
	threads = mainthread;
	
	mainthread->main = 1;
	mainthread->run = 1;
	mainthread->shutdown = 0;
	
	iohandler_worker(1);
	
	_stop_iodns(); /* possible worker thread */
}

