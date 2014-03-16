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
#include "IOLog.h"

#include <sys/time.h>
#include <stdlib.h>

struct IOGCObject {
	void *object;
	iogc_free *free_callback;
	struct timeval timeout;
	
	struct IOGCObject *next;
};

static int iogc_enabled = 1;
static struct timeval iogc_timeout;
static struct IOGCObject *first_object = NULL, *last_object = NULL;

void iogc_init() {
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
	obj->timeout.tv_sec += IOGC_TIMEOUT;
	
	obj->next = NULL;
	if(last_object)
		last_object->next = obj;
	else
		first_object = obj;
	last_object = obj;
}

void iogc_exec() {
	struct timeval now;
	gettimeofday(&now, NULL);
	
	struct IOGCObject *obj, *next_obj;
	for(obj = first_object; obj; obj = next_obj) {
		if(timeval_is_smaler(obj->timeout, now)) {
			next_obj = obj->next;
			if(obj->free_callback)
				obj->free_callback(obj->object);
			else
				free(obj->object);
			free(obj);
		} else
			break;
	}
	first_object = obj;
}
