/* IOEngine_epoll.c - IOMultiplexer
 * Copyright (C) 2012  Philipp Kreil (pk910)
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
#include "IOEngine.h"

#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define MAX_EVENTS 32

static int epoll_fd;

static int engine_epoll_init() {
    epoll_fd = epoll_create(1024);
    if (epoll_fd < 0)
        return 0;
    return 1;
}

static void engine_epoll_add(struct IODescriptor *iofd) {
    if(iofd->type == IOTYPE_TIMER) return;
    //add descriptor to the epoll queue
    struct epoll_event evt;
    int res;

    evt.events = EPOLLHUP | EPOLLIN | (iohandler_wants_writes(iofd) ? EPOLLOUT : 0);
    evt.data.ptr = iofd;
    res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, iofd->fd, &evt);
    if(res < 0) {
        iohandler_log(IOLOG_ERROR, "could not add IODescriptor %d to epoll queue. (returned: %d)", iofd->fd, res);
    }
}

static void engine_epoll_remove(struct IODescriptor *iofd) {
    if(iofd->type == IOTYPE_TIMER) return;
    struct epoll_event evt;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, iofd->fd, &evt);
}

static void engine_epoll_update(struct IODescriptor *iofd) {
    if(iofd->type == IOTYPE_TIMER) return;
    if(iofd->state == IO_CLOSED) {
        engine_epoll_remove(iofd);
        return;
    }
    struct epoll_event evt;
    int res;

    evt.events = EPOLLHUP | EPOLLIN | (iohandler_wants_writes(iofd) ? EPOLLOUT : 0);
    evt.data.ptr = iofd;
    res = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, iofd->fd, &evt);
    if(res < 0) {
        iohandler_log(IOLOG_ERROR, "could not update IODescriptor %d in epoll queue. (returned: %d)", iofd->fd, res);
    }
}

static void engine_epoll_loop(struct timeval *timeout) {
    struct epoll_event evts[MAX_EVENTS];
    struct timeval now, tdiff;
    int msec;
    int events;
    int epoll_result;
    
    gettimeofday(&now, NULL);
    
    while(timer_priority) {
        tdiff.tv_sec = timer_priority->timeout.tv_sec - now.tv_sec;
        tdiff.tv_usec = timer_priority->timeout.tv_usec - now.tv_usec;
        if(tdiff.tv_sec < 0 || (tdiff.tv_sec == 0 && tdiff.tv_usec <= 0)) {
            if(timer_priority->constant_timeout) {
                tdiff.tv_sec = 0;
                iohandler_set_timeout(timer_priority, &tdiff);
                iohandler_events(timer_priority, 0, 0);
            } else {
                iohandler_events(timer_priority, 0, 0);
                iohandler_close(timer_priority); //also sets timer_priority to the next timed element
            }
            continue;
        } else if(tdiff.tv_usec < 0) {
            tdiff.tv_sec--;
            tdiff.tv_usec += 1000000; //1 sec
        }
        if(timeval_is_smaler((&tdiff), timeout)) {
            timeout->tv_sec = tdiff.tv_sec;
            timeout->tv_usec = tdiff.tv_usec;
        }
        break;
    }
    
    msec = timeout ? ((timeout->tv_sec * 1000 + timeout->tv_usec / 1000) + (timeout->tv_usec % 1000 != 0 ? 1 : 0)) : -1;
    
    //select system call
    epoll_result = epoll_wait(epoll_fd, evts, MAX_EVENTS, msec);
    
    if (epoll_result < 0) {
        if (errno != EINTR) {
            iohandler_log(IOLOG_FATAL, "epoll_wait() failed with errno %d: %s", errno, strerror(errno));
            return;
        }
    } else {
        int i;
        for(i = 0; i < epoll_result; i++) {
            events = evts[i].events;
            iohandler_events(evts[i].data.ptr, (events & (EPOLLIN | EPOLLHUP)), (events & EPOLLOUT));
        }
    }
    
    //check timers
    while(timer_priority) {
        tdiff.tv_sec = timer_priority->timeout.tv_sec - now.tv_sec;
        tdiff.tv_usec = timer_priority->timeout.tv_usec - now.tv_usec;
        if(tdiff.tv_sec < 0 || (tdiff.tv_sec == 0 && tdiff.tv_usec <= 0)) {
            if(timer_priority->constant_timeout) {
                tdiff.tv_sec = 0;
                iohandler_set_timeout(timer_priority, &tdiff);
                iohandler_events(timer_priority, 0, 0);
            } else {
                iohandler_events(timer_priority, 0, 0);
                iohandler_close(timer_priority); //also sets timer_priority to the next timed element
            }
            continue;
        }
        break;
    }
    
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
