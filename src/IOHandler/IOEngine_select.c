/* IOEngine_select.c - IOMultiplexer
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

#include <errno.h>
#include <time.h>
#ifdef WIN32
#define _WIN32_WINNT 0x501
#include <windows.h>
#include <winsock2.h>
#else
#include <string.h>
#include <stdio.h>
#endif

/* compat */
#include "compat/utime.h"

static int engine_select_init() {
	return 1;
}

static void engine_select_add(struct _IOSocket *iosock) {
	/* empty */
}

static void engine_select_remove(struct _IOSocket *iosock) {
	/* empty */
}

static void engine_select_update(struct _IOSocket *iosock) {
	/* empty */
}

static void engine_select_loop(struct timeval *timeout) {
	fd_set read_fds;
	fd_set write_fds;
	unsigned int fds_size = 0;
	struct _IOSocket *iosock, *next_iosock;
	struct timeval now, tout;
	int select_result;
	
	//clear fds
	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	
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
	
	select_result = 0;
	for(iosock = iosocket_first; iosock; iosock = iosock->next) {
		if(!(iosock->socket_flags & IOSOCKETFLAG_ACTIVE)) 
			continue;
		if(iosock->fd > fds_size)
			fds_size = iosock->fd;
		FD_SET(iosock->fd, &read_fds);
		select_result++;
		if(iosocket_wants_writes(iosock))
			FD_SET(iosock->fd, &write_fds);
	}

	if(select_result) //select system call
		select_result = select(fds_size + 1, &read_fds, &write_fds, NULL, timeout);
	else if(timeout) {
		usleep_tv(*timeout);
		select_result = 0;
	} else
		usleep(10000); // 10ms
	
	if (select_result < 0) {
		if (errno != EINTR) {
			iolog_trigger(IOLOG_FATAL, "select() failed with errno %d %d: %s", select_result, errno, strerror(errno));
			return;
		}
	}
	
	gettimeofday(&now, NULL);
	
	//check all descriptors
	for(iosock = iosocket_first; iosock; iosock = next_iosock) {
		next_iosock = iosock->next;
		if(!(iosock->socket_flags & IOSOCKETFLAG_ACTIVE))
			return;
		if(FD_ISSET(iosock->fd, &read_fds) || FD_ISSET(iosock->fd, &write_fds))
			iosocket_events_callback(iosock, FD_ISSET(iosock->fd, &read_fds), FD_ISSET(iosock->fd, &write_fds));
	}
	
	//check timers
	if(iotimer_sorted_descriptors && timeval_is_bigger(now, iotimer_sorted_descriptors->timeout))
		_trigger_timer();
	
}

static void engine_select_cleanup() {
	/* empty */
}

struct IOEngine engine_select = {
	.name = "select",
	.init = engine_select_init,
	.add = engine_select_add,
	.remove = engine_select_remove,
	.update = engine_select_update,
	.loop = engine_select_loop,
	.cleanup = engine_select_cleanup,
};
