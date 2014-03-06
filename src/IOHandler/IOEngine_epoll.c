/* IOEngine_epoll.c - IOMultiplexer
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
#include "IOTimer.h"

#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define MAX_EVENTS 32

static int epoll_fd;

static int engine_epoll_init() {
	epoll_fd = epoll_create(IOHANDLER_MAX_SOCKETS);
	if (epoll_fd < 0)
		return 0;
	return 1;
}

static void engine_epoll_add(struct _IOSocket *iosock) {
	//add Socket FD to the epoll queue
	struct epoll_event evt;
	int res;

	evt.events = EPOLLHUP | (iosocket_wants_reads(iosock) ? EPOLLIN : 0) | (iosocket_wants_writes(iosock) ? EPOLLOUT : 0);
	evt.data.ptr = iosock;
	res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, iosock->fd, &evt);
	if(res < 0)
		iolog_trigger(IOLOG_ERROR, "could not add _IOSocket %d to epoll queue. (returned: %d)", iosock->fd, res);
}

static void engine_epoll_remove(struct _IOSocket *iosock) {
	struct epoll_event evt;
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, iosock->fd, &evt);
}

static void engine_epoll_update(struct _IOSocket *iosock) {
	struct epoll_event evt;
	int res;

	evt.events = EPOLLHUP | (iosocket_wants_reads(iosock) ? EPOLLIN : 0) | (iosocket_wants_writes(iosock) ? EPOLLOUT : 0);
	evt.data.ptr = iosock;
	res = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, iosock->fd, &evt);
	if(res < 0)
		iolog_trigger(IOLOG_ERROR, "could not update _IOSocket %d in epoll queue. (returned: %d)", iosock->fd, res);
}

static void engine_epoll_loop(struct timeval *timeout) {
	struct epoll_event evts[MAX_EVENTS];
	int msec, msec2;
	int events;
	int epoll_result;
	struct timeval now;
	
	//check timers
	gettimeofday(&now, NULL);
	if(iotimer_sorted_descriptors && timeval_is_bigger(now, iotimer_sorted_descriptors->timeout))
		_trigger_timer();
	
	//get timeout (timer or given timeout)
	if(iotimer_sorted_descriptors) {
		msec = (iotimer_sorted_descriptors->timeout.tv_sec - now.tv_sec) * 1000;
		msec += (iotimer_sorted_descriptors->timeout.tv_usec - now.tv_usec) / 1000;
	}
	if(timeout) {
		msec2 = (timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
		if(!iotimer_sorted_descriptors || msec2 < msec)
			msec = msec2;
	} else if(!iotimer_sorted_descriptors)
		msec = -1;
	
	//epoll system call
	epoll_result = epoll_wait(epoll_fd, evts, MAX_EVENTS, msec);
	
	if (epoll_result < 0) {
		if (errno != EINTR) {
			iolog_trigger(IOLOG_FATAL, "epoll_wait() failed with errno %d: %s", errno, strerror(errno));
			return;
		}
	} else {
		int i;
		for(i = 0; i < epoll_result; i++) {
			events = evts[i].events;
			iosocket_events_callback(evts[i].data.ptr, (events & (EPOLLIN | EPOLLHUP)), (events & EPOLLOUT));
		}
	}
	
	//check timers
	gettimeofday(&now, NULL);
	if(iotimer_sorted_descriptors && timeval_is_bigger(now, iotimer_sorted_descriptors->timeout))
		_trigger_timer();
}

static void engine_epoll_cleanup() {
	close(epoll_fd);
}

struct IOEngine engine_epoll = {
	.name = "epoll",
	.init = engine_epoll_init,
	.add = engine_epoll_add,
	.remove = engine_epoll_remove,
	.update = engine_epoll_update,
	.loop = engine_epoll_loop,
	.cleanup = engine_epoll_cleanup,
};

#else

struct IOEngine engine_epoll = {
	.name = "epoll",
	.init = NULL,
	.add = NULL,
	.remove = NULL,
	.update = NULL,
	.loop = NULL,
	.cleanup = NULL,
};

#endif
