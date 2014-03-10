/* IODNSEngine_cares.c - IOMultiplexer
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
#include "IOTimer.h"

#ifdef HAVE_ARES_H
#include <ares.h>
#include <string.h>
#include <sys/time.h>
#ifdef WIN32
#define _WIN32_WINNT 0x501
#include <windows.h>
#include <winsock2.h>
#elif defined HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

struct dnsengine_cares_socket {
	struct _IOSocket *iosock;
	int want_read : 1;
	int want_write : 1;
};

static IOTIMER_CALLBACK(dnsengine_cares_timer_callback);

static ares_channel dnsengine_cares_channel;
static struct dnsengine_cares_socket dnsengine_cares_sockets[ARES_GETSOCK_MAXNUM];
static struct IOTimerDescriptor *dnsengine_cares_timer = NULL;

static int dnsengine_cares_init() {
	int res;
	
	// zero dnsengine_cares_sockets array
	memset(dnsengine_cares_sockets, 0, sizeof(*dnsengine_cares_sockets) * ARES_GETSOCK_MAXNUM);
	
	// initialize cares
	if((res = ares_init(&dnsengine_cares_channel)) != ARES_SUCCESS) {
		iolog_trigger(IOLOG_ERROR, "Failed to initialize c-ares in %s:%d", __FILE__, __LINE__);
        return 0;
    }
    return 0; /* backend not completed */
}

static void dnsengine_cares_update_sockets() {
	int ares_socks[ARES_GETSOCK_MAXNUM];
	memset(ares_socks, 0, sizeof(*ares_socks) * ARES_GETSOCK_MAXNUM);
	int sockreqs = ares_getsock(dnsengine_cares_channel, ares_socks, ARES_GETSOCK_MAXNUM);
	int i, j, sockid, newsock, updatesock;
	struct _IOSocket *iosock;
	
	//unregister "old" sockets
	for(i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
		if(!dnsengine_cares_sockets[i].iosock)
			continue;
		
		//search matching ares_socks
		sockid = -1;
		for(j = 0; j < ARES_GETSOCK_MAXNUM; j++) {
			if(dnsengine_cares_sockets[i].iosock->fd == ares_socks[j]) {
				sockid = j;
				break;
			}
		}
		if(sockid == -1) {
			//unregister socket
			_free_socket(dnsengine_cares_sockets[i].iosock);
			dnsengine_cares_sockets[i].iosock = NULL;
		}
	}
	
	//register new / update existing sockets
	for(i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
		if(!ares_socks[i])
			break;
		
		//search matching dnsengine_cares_socket
		sockid = -1;
		for(j = 0; j < ARES_GETSOCK_MAXNUM; j++) {
			if(dnsengine_cares_sockets[j].iosock && dnsengine_cares_sockets[j].iosock->fd == ares_socks[i]) {
				sockid = j;
				break;
			}
		}
		
		if(sockid == -1) {
			//append new socket
			for(j = 0; j < ARES_GETSOCK_MAXNUM; j++) {
				if(!dnsengine_cares_sockets[j].iosock) {
					sockid = j;
					break;
				}
			}
			if(sockid == -1) {
				iolog_trigger(IOLOG_ERROR, "Error in dnsengine_cares_update_sockets: could not find free dnsengine_cares_socket in %s:%d", __FILE__, __LINE__);
				continue;
			}
			iosock = _create_socket();
			if(!iosock)
				continue;
			
			//set up iosock
			iosock->socket_flags |= IOSOCKETFLAG_PARENT_DNSENGINE | IOSOCKETFLAG_OVERRIDE_WANT_RW;
			iosock->fd = ares_socks[i];
			dnsengine_cares_sockets[sockid].want_read = 0;
			dnsengine_cares_sockets[sockid].want_write = 0;
			
			newsock = 1;
		} else
			newsock = 0;
		
		updatesock = 0;
		if(dnsengine_cares_sockets[sockid].want_read ^ ARES_GETSOCK_READABLE(sockreqs, i)) {
			if(ARES_GETSOCK_READABLE(sockreqs, i)) {
				dnsengine_cares_sockets[sockid].iosock->socket_flags |= IOSOCKETFLAG_OVERRIDE_WANT_R;
				dnsengine_cares_sockets[sockid].want_read = 1;
			} else {
				dnsengine_cares_sockets[sockid].iosock->socket_flags &= ~IOSOCKETFLAG_OVERRIDE_WANT_R;
				dnsengine_cares_sockets[sockid].want_read = 0;
			}
			updatesock = 1;
		}
		if(dnsengine_cares_sockets[sockid].want_write ^ ARES_GETSOCK_WRITABLE(sockreqs, i)) {
			if(ARES_GETSOCK_WRITABLE(sockreqs, i)) {
				dnsengine_cares_sockets[sockid].iosock->socket_flags |= IOSOCKETFLAG_OVERRIDE_WANT_W;
				dnsengine_cares_sockets[sockid].want_write = 1;
			} else {
				dnsengine_cares_sockets[sockid].iosock->socket_flags &= ~IOSOCKETFLAG_OVERRIDE_WANT_W;
				dnsengine_cares_sockets[sockid].want_write = 0;
			}
			updatesock = 1;
		}
		if(updatesock || newsock) {
			if(newsock)
				iosocket_activate(dnsengine_cares_sockets[sockid].iosock);
			else
				iosocket_update(dnsengine_cares_sockets[sockid].iosock);
		}
	}
}

static void dnsengine_cares_update_timeout() {
	struct timeval timeout, now;
	timeout.tv_sec = 60;
	timeout.tv_usec = 0;
	ares_timeout(dnsengine_cares_channel, &timeout, &timeout);
	
	gettimeofday(&now, NULL);
	timeout.tv_sec += now.tv_sec;
	timeout.tv_usec += now.tv_usec;
	if(timeout.tv_usec > 1000000) {
		timeout.tv_sec += 1;
		timeout.tv_usec -= 1000000;
	}
	
	if(dnsengine_cares_timer)
		iotimer_set_timeout(dnsengine_cares_timer, &timeout);
	else {
		dnsengine_cares_timer = iotimer_create(&timeout);
		iotimer_set_callback(dnsengine_cares_timer, dnsengine_cares_timer_callback);
		iotimer_start(dnsengine_cares_timer);
	}
}

static IOTIMER_CALLBACK(dnsengine_cares_timer_callback) {
	dnsengine_cares_timer = NULL;
	ares_process_fd(dnsengine_cares_channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
	dnsengine_cares_update_timeout();
	dnsengine_cares_update_sockets();
}

static void dnsengine_cares_socket_callback(struct _IOSocket *iosock, int wantread, int wantwrite) {
	int socketfd = iosock->fd;
	ares_process_fd(dnsengine_cares_channel, (wantread ? socketfd : ARES_SOCKET_BAD), (wantread ? socketfd : ARES_SOCKET_BAD));
	dnsengine_cares_update_timeout();
	dnsengine_cares_update_sockets();
}

static void dnsengine_cares_stop() {
	if(dnsengine_cares_timer)
		iotimer_destroy(dnsengine_cares_timer);
}



static void dnsengine_cares_add(struct _IODNSQuery *iodns) {
    /* TODO */
}

static void dnsengine_cares_remove(struct _IODNSQuery *iodns) {
    /* TODO */
}

static void dnsengine_cares_loop() {
    /* empty */
}

struct IODNSEngine dnsengine_cares = {
    .name = "c-ares",
    .init = dnsengine_cares_init,
	.stop = dnsengine_cares_stop,
    .add = dnsengine_cares_add,
    .remove = dnsengine_cares_remove,
    .loop = dnsengine_cares_loop,
	.socket_callback = dnsengine_cares_socket_callback,
};

#else

struct IODNSEngine dnsengine_cares = {
    .name = "c-ares",
    .init = NULL,
	.stop = NULL,
    .add = NULL,
    .remove = NULL,
    .loop = NULL,
	.socket_callback = NULL,
};

#endif
