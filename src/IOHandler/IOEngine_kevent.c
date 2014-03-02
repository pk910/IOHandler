/* IOengine_kevent.c - IOMultiplexer
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
#include "IOSockets.h"

#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#include <errno.h>

#define MAX_EVENTS 32

static int kevent_fd;

static int engine_kevent_init() {
	kevent_fd = kqueue();
	if (kevent_fd < 0)
		return 0;
	return 1;
}

static void engine_kevent_add(struct _IOSocket *iosock) {
	//add Socket FD to the kevent queue
	struct kevent changes[2];
	int nchanges = 0;
	int res;

	EV_SET(&changes[nchanges++], iosock->fd, EVFILT_READ, EV_ADD, 0, 0, iosock);
	if (iosocket_wants_writes(iosock))
		EV_SET(&changes[nchanges++], iosock->fd, EVFILT_WRITE, EV_ADD, 0, 0, iosock);
	
	res = kevent(kevent_fd, changes, nchanges, NULL, 0, NULL);
	if(res < 0)
		iolog_trigger(IOLOG_ERROR, "could not add _IOSocket %d to kevent queue. (returned: %d)", iosock->fd, res);
}

static void engine_kevent_remove(struct _IOSocket *iosock) {
	struct kevent changes[2];
	int nchanges = 0;

	EV_SET(&changes[nchanges++], iosock->fd, EVFILT_READ, EV_DELETE, 0, 0, iosock);
	EV_SET(&changes[nchanges++], iosock->fd, EVFILT_WRITE, EV_DELETE, 0, 0, iosock);
	kevent(kevent_fd, changes, nchanges, NULL, 0, NULL);
}

static void engine_kevent_update(struct _IOSocket *iosock) {
	struct kevent changes[2];
	int nchanges = 0;
	int res;

	EV_SET(&changes[nchanges++], iosock->fd, EVFILT_READ, EV_ADD, 0, 0, iosock);
	EV_SET(&changes[nchanges++], iosock->fd, EVFILT_WRITE, iosocket_wants_writes(iosock) ? EV_ADD : EV_DELETE, 0, 0, iosock);
	
	res = kevent(kevent_fd, changes, nchanges, NULL, 0, NULL);
	if(res < 0)
		iolog_trigger(IOLOG_ERROR, "could not update _IOSocket %d in kevent queue. (returned: %d)", iosock->fd, res);
}

static void engine_kevent_loop(struct timeval *timeout) {
	struct kevent events[MAX_EVENTS];
	struct timespec ts;
	int msec;
	int kevent_result;
	struct timeval now, tout;
	
	//check timers
	gettimeofday(&now, NULL);
	if(iotimer_sorted_descriptors && timeval_is_bigger(now, iotimer_sorted_descriptors->timeout))
		_trigger_timer();
	
	//get timeout (timer or given timeout)
	if(iotimer_sorted_descriptors) {
		tout = iotimer_sorted_descriptors->timeout;
		tout.tv_sec -= now.tv_sec;
		tout.tv_usec -= now.tv_usec;
		if(tout.tv_usec < 0) {
			tout.tv_sec --;
			tout.tv_usec += 1000000;
		}
	}
	if(timeout) {
		if(!iotimer_sorted_descriptors || timeval_is_smaler((*timeout), tout)) {
			tout.tv_usec = timeout->tv_usec;
			tout.tv_sec = timeout->tv_sec;
		}
		timeout = &tout;
	} else if(iotimer_sorted_descriptors)
		timeout = &tout;
	
	
	//select system call
	kevent_result = kevent(kevent_fd, NULL, 0, events, MAX_EVENTS, timeout);
	
	if (kevent_result < 0) {
		if (errno != EINTR) {
			iolog_trigger(IOLOG_FATAL, "kevent() failed with errno %d: %s", errno, strerror(errno));
			return;
		}
	} else {
		int i;
		for(i = 0; i < kevent_result; i++)
			iosocket_events_callback(events[i].udata, (events[i].filter == EVFILT_READ), (events[i].filter == EVFILT_WRITE));
	}
	
	//check timers
	gettimeofday(&now, NULL);
	if(iotimer_sorted_descriptors && timeval_is_bigger(now, iotimer_sorted_descriptors->timeout))
		_trigger_timer();
}

static void engine_kevent_cleanup() {
	close(kevent_fd);
}

struct IOEngine engine_kevent = {
	.name = "kevent",
	.init = engine_kevent_init,
	.add = engine_kevent_add,
	.remove = engine_kevent_remove,
	.update = engine_kevent_update,
	.loop = engine_kevent_loop,
	.cleanup = engine_kevent_cleanup,
};

#else

struct IOEngine engine_kevent = {
	.name = "kevent",
	.init = NULL,
	.add = NULL,
	.remove = NULL,
	.update = NULL,
	.loop = NULL,
	.cleanup = NULL,
};

#endif
