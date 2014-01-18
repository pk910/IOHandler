/* IOGarbageCollector.c - IOMultiplexer v2
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
#include "IOGarbageCollector.h"

#define timeval_is_bigger(x,y) ((x.tv_sec > y.tv_sec) || (x.tv_sec == y.tv_sec && x.tv_usec > y.tv_usec))
#define timeval_is_smaler(x,y) ((x.tv_sec < y.tv_sec) || (x.tv_sec == y.tv_sec && x.tv_usec < y.tv_usec))

static struct IOGCObject {
	void *object;
	iogc_free *free_callback;
	struct timeval timeout;
	
	struct IOGCObject *next;
};

#ifdef HAVE_PTHREAD_H
static pthread_mutex_t iogc_sync;
#endif

static int iogc_enabled = 1;
static struct timeval iogc_timeout;
static struct IOGCObject *first_object = NULL, *last_object = NULL;

void iogc_init() {
	IOTHREAD_MUTEX_INIT(iogc_sync);
	iogc_timeout.tv_usec = 0;
	iogc_timeout.tv_sec = 10;
}


void iohandler_set_gc(int enabled) {
	if(enabled)
		iogc_enabled = 1;
	else
		iogc_enabled = 0;
}

void iogc_add(void *object) {
	iogc_add_callback(object, NULL);
}

void iogc_add_callback(void *object, iogc_free *free_callback) {
	if(!iogc_enabled) {
		if(free_callback)
			free_callback(object);
		else
			free(object);
		return;
	}
	struct IOGCObject *obj = malloc(sizeof(*obj));
    if(!obj) {
        iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOGCObject in %s:%d", __FILE__, __LINE__);
		if(free_callback)
			free_callback(object);
		else
			free(object);
        return;
    }
	obj->object = object;
	obj->free_callback = free_callback;
	gettimeofday(&obj->timeout, NULL);
	
}

void iogc_exec() {
	struct timeval ctime;
	gettimeofday(&ctime, NULL);
	
	struct IOGCObject *obj, *next_obj;
	for(obj = objects; obj; obj = next_obj) {
		if(timeval_is_smaler(obj->timeout, ctime)) {
			next_obj = obj->next;
			if(obj->free_callback)
				obj->free_callback(obj->object);
			else
				free(obj->object);
			free(obj);
		} else {
			objects = obj;
			break;
		}
	}
}
