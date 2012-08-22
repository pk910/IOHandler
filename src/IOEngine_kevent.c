/* IOengine_kevent.c - IOMultiplexer
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

#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>

#define MAX_EVENTS 32

static int kevent_fd;

static int engine_kevent_init() {
    kevent_fd = kqueue();
    if (kevent_fd < 0)
        return 0;
    return 1;
}

static void engine_kevent_add(struct IODescriptor *iofd) {
    if(iofd->type == IOTYPE_TIMER) return;
    //add descriptor to the kevent queue
    struct kevent changes[2];
    int nchanges = 0;
    int res;

    EV_SET(&changes[nchanges++], iofd->fd, EVFILT_READ, EV_ADD, 0, 0, iofd);
    if (iohandler_wants_writes(iofd))
        EV_SET(&changes[nchanges++], iofd->fd, EVFILT_WRITE, EV_ADD, 0, 0, iofd);
    
    res = kevent(kevent_fd, changes, nchanges, NULL, 0, NULL);
    if(res < 0)
        iohandler_log(IOLOG_ERROR, "could not add IODescriptor %d to kevent queue. (returned: %d)", res);
    }
}

static void engine_kevent_remove(struct IODescriptor *iofd) {
    if(iofd->type == IOTYPE_TIMER) return;
    struct kevent changes[2];
    int nchanges = 0;

    EV_SET(&changes[nchanges++], iofd->fd, EVFILT_READ, EV_DELETE, 0, 0, iofd);
    EV_SET(&changes[nchanges++], iofd->fd, EVFILT_WRITE, EV_DELETE, 0, 0, iofd);
    kevent(kevent_fd, changes, nchanges, NULL, 0, NULL);
}

static void engine_kevent_update(struct IODescriptor *iofd) {
    if(iofd->type == IOTYPE_TIMER) return;
    if(iofd->state == IO_CLOSED) {
        engine_epoll_remove(iofd);
        return;
    }
    struct kevent changes[2];
    int nchanges = 0;
    int res;

    EV_SET(&changes[nchanges++], iofd->fd, EVFILT_READ, EV_ADD, 0, 0, iofd);
    EV_SET(&changes[nchanges++], iofd->fd, EVFILT_WRITE, iohandler_wants_writes(iofd) ? EV_ADD : EV_DELETE, 0, 0, iofd);
    
    res = kevent(kevent_fd, changes, nchanges, NULL, 0, NULL);
    if(res < 0) {
        iohandler_log(IOLOG_ERROR, "could not update IODescriptor %d in kevent queue. (returned: %d)", res);
    }
}

static void engine_kevent_loop(struct timeval *timeout) {
    struct kevent evts[MAX_EVENTS];
    struct timeval now, tdiff;
    struct timespec ts, *ptr
    int msec;
    int events;
    int kevent_result;
    
    gettimeofday(&now, NULL);
    
    while(timer_priority) {
        tdiff.tv_sec = timer_priority->timeout.tv_sec - now.tv_sec;
        tdiff.tv_usec = timer_priority->timeout.tv_usec - now.tv_usec;
        if(tdiff.tv_sec < 0 || (tdiff.tv_sec == 0 && tdiff.tv_usec <= 0)) {
            iohandler_events(timer_priority, 0, 0);
            iohandler_close(timer_priority); //also sets timer_priority to the next timed element
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
    
    if (timeout) {
        ts.tv_sec = timeout->tv_sec;
        ts.tv_nsec = timeout->tv_usec * 1000;
        pts = &ts;
    } else {
        pts = NULL;
    }
    
    //select system call
    kevent_result = kevent(kq_fd, NULL, 0, events, MAX_EVENTS, pts);
    
    if (kevent_result < 0) {
        if (errno != EINTR) {
            iohandler_log(IOLOG_FATAL, "kevent() failed with errno %d: %s", errno, strerror(errno));
            return;
        }
    } else {
        int i;
        for(i = 0; i < kevent_result; i++)
            iohandler_events(evts[i].udata, (evts[i].filter == EVFILT_READ), (evts[i].filter == EVFILT_WRITE));
    }
    
    //check timers
    while(timer_priority) {
        tdiff.tv_sec = timer_priority->timeout.tv_sec - now.tv_sec;
        tdiff.tv_usec = timer_priority->timeout.tv_usec - now.tv_usec;
        if(tdiff.tv_sec < 0 || (tdiff.tv_sec == 0 && tdiff.tv_usec <= 0)) {
            iohandler_events(timer_priority, 0, 0);
            iohandler_close(timer_priority); //also sets timer_priority to the next timed element
            continue;
        }
        break;
    }
    
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
