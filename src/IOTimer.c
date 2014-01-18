/* IOTimer.c - IOMultiplexer v2
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
#include "IOHandlerTimer.h"
#include "IOLog.h"

#ifdef HAVE_PTHREAD_H
static pthread_mutex_t iotimer_sync;
#endif

#define timeval_is_bigger(x,y) ((x->tv_sec > y->tv_sec) || (x->tv_sec == y->tv_sec && x->tv_usec > y->tv_usec))
#define timeval_is_smaler(x,y) ((x->tv_sec < y->tv_sec) || (x->tv_sec == y->tv_sec && x->tv_usec < y->tv_usec))

static void _rearrange_timer(struct IOTimerDescriptor *timer);
static void _autoreload_timer(struct IOTimerDescriptor *timer);

struct _IOTimerDescriptor *iotimer_sorted_descriptors;

/* public functions */

struct IOTimerDescriptor iotimer_create(struct timeval *timeout) {
	struct IOTimerDescriptor *descriptor = calloc(1, sizeof(*descriptor));
    if(!descriptor) {
        iolog_trigger(IOLOG_ERROR, "could not allocate memory for IOTimerDescriptor in %s:%d", __FILE__, __LINE__);
        return NULL;
    }
	struct _IOTimerDescriptor *timer = _create_timer(timeout);
	if(!timer) {
		free(descriptor);
		return NULL;
	}
	timer->parent = descriptor;
	timer->flags |= IOTIMERFLAG_PARENT_PUBLIC;
	descriptor->iotimer = timer;
	
	return descriptor;
}

void iotimer_start(struct IOTimerDescriptor *descriptor) {
	struct _IOTimerDescriptor *timer = descriptor->iotimer;
	if(timer == NULL) {
		iolog_trigger(IOLOG_WARNING, "called iotimer_set_autoreload for destroyed IOTimerDescriptor in %s:%d", __FILE__, __LINE__);
		return;
	}
	timer->flags |= IOTIMERFLAG_ACTIVE;
	if(!(timer->flags & IOTIMERFLAG_IN_LIST))
		_trigger_timer(timer);
}

void iotimer_set_autoreload(struct IOTimerDescriptor *descriptor, struct timeval *autoreload) {
	struct _IOTimerDescriptor *timer = descriptor->iotimer;
	if(timer == NULL) {
		iolog_trigger(IOLOG_WARNING, "called iotimer_set_autoreload for destroyed IOTimerDescriptor in %s:%d", __FILE__, __LINE__);
		return;
	}
	if(autoreload) {
		timer->flags |= IOTIMERFLAG_PERIODIC;
		timer->autoreload = *autoreload;
		
		if(!(timer->flags & IOTIMERFLAG_IN_LIST)) {
			struct timeval ctime;
			gettimeofday(&ctime, NULL);
			timer->timeout = ctime;
			_autoreload_timer(timer);
		}
	} else {
		timer->flags &= ~IOTIMERFLAG_PERIODIC;
	}
}

void iotimer_set_callback(struct IOTimerDescriptor *descriptor, iotimer_callback *callback) {
	descriptor->callback = callback;
}

void iotimer_destroy(struct IOTimerDescriptor *descriptor) {
	struct _IOTimerDescriptor *timer = descriptor->iotimer;
	if(timer == NULL) {
		iolog_trigger(IOLOG_WARNING, "called iotimer_destroy for destroyed IOTimerDescriptor in %s:%d", __FILE__, __LINE__);
		return;
	}
	descriptor->iotimer = NULL;
	_destroy_timer(timer);
	
	iogc_add(descriptor);
}

/* internal functions */
void _init_timers() {
	IOTHREAD_MUTEX_INIT(iotimer_sync);
}

struct _IOTimerDescriptor _create_timer(struct timeval *timeout) {
	struct _IOTimerDescriptor *timer = calloc(1, sizeof(*timer));
    if(!timer) {
        iolog_trigger(IOLOG_ERROR, "could not allocate memory for _IOTimerDescriptor in %s:%d", __FILE__, __LINE__);
        return NULL;
    }
	if(timeout) {
		timer->timeout = *timeout;
		_rearrange_timer(timer);
	}
	return timer;
}

static void _rearrange_timer(struct IOTimerDescriptor *timer) {
	IOSYNCHRONIZE(iotimer_sync);
	if((timer->flags & IOTIMERFLAG_IN_LIST)) {
		if(timer->prev == NULL)
			iotimer_sorted_descriptors = timer->next;
		else
			timer->prev->next = timer->next;
		if(timer->next != NULL)
			timer->next->prev = timer->prev;
	}
	struct _IOTimerDescriptor *ctimer;
	for(ctimer = iotimer_sorted_descriptors; ctimer;) {
		if(timeval_is_bigger(&ctimer->timeout, &timer->timeout)) {
			timer->next = ctimer;
			timer->prev = ctimer->prev;
			if(ctimer->prev)
				ctimer->prev->next = timer;
			else
				iotimer_sorted_descriptors = timer;
			ctimer->prev = timer;
			break
		}
		else if(ctimer->next == NULL) {
			ctimer->next = timer;
			timer->prev = ctimer;
			break;
		}
	}
	if(ctimer == NULL)
		iotimer_sorted_descriptors = timer;
	timer->flags |= IOTIMERFLAG_IN_LIST;
	IODESYNCHRONIZE(iotimer_sync);
}

void _destroy_timer(struct _IOTimerDescriptor *timer) {
	if((timer->flags & IOTIMERFLAG_IN_LIST)) {
		IOSYNCHRONIZE(iotimer_sync);
		if(timer->prev == NULL)
			iotimer_sorted_descriptors = timer->next;
		else
			timer->prev->next = timer->next;
		if(timer->next != NULL)
			timer->next->prev = timer->prev;
		IODESYNCHRONIZE(iotimer_sync);
	}
	free(timer);
}

static void _autoreload_timer(struct IOTimerDescriptor *timer) {
	timer->timeout.tv_usec += timer->autoreload.tv_usec;
	timer->timeout.tv_sec += timer->autoreload.tv_sec;
	if(timer->timeout.tv_usec > 1000000) {
		timer->timeout.tv_sec += (timer->timeout.tv_usec / 1000000);
		timer->timeout.tv_usec %= 1000000;
	}
	_rearrange_timer(timer);
}

