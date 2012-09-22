/* IOEngine.h - IOMultiplexer
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
#ifndef _IOEngine_h
#define _IOEngine_h
#include "IOHandler.h"

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#ifdef PTHREAD_MUTEX_RECURSIVE_NP
#define PTHREAD_MUTEX_RECURSIVE_VAL PTHREAD_MUTEX_RECURSIVE_NP
#else
#define PTHREAD_MUTEX_RECURSIVE_VAL PTHREAD_MUTEX_RECURSIVE
#endif
#define IOTHREAD_MUTEX_INIT(var) { \
    pthread_mutexattr_t mutex_attr; \
    pthread_mutexattr_init(&mutex_attr);\
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE_VAL);\
    pthread_mutex_init(&var, &mutex_attr); \
}
#define IOSYNCHRONIZE(var) pthread_mutex_lock(&var)
#define IODESYNCHRONIZE(var) pthread_mutex_unlock(&var)
#else
#define IOTHREAD_MUTEX_INIT(var)
#define IOSYNCHRONIZE(var)
#define IODESYNCHRONIZE(var)
#endif

struct IODescriptor;
enum IOType;
enum IOStatus;
enum IOEventType;

#define timeval_is_bigger(x,y) ((x->tv_sec > y->tv_sec) || (x->tv_sec == y->tv_sec && x->tv_usec > y->tv_usec))
#define timeval_is_smaler(x,y) ((x->tv_sec < y->tv_sec) || (x->tv_sec == y->tv_sec && x->tv_usec < y->tv_usec))

extern struct IODescriptor *first_descriptor;
extern struct IODescriptor *timer_priority;

struct IOEngine {
    const char *name;
    int (*init)(void);
    void (*add)(struct IODescriptor *iofd);
    void (*remove)(struct IODescriptor *iofd);
    void (*update)(struct IODescriptor *iofd);
    void (*loop)(struct timeval *timeout);
    void (*cleanup)(void);
};

#define iohandler_wants_writes(IOFD) ((IOFD->writebuf.bufpos && !IOFD->ssl_hs_read) || IOFD->state == IO_CONNECTING || IOFD->ssl_hs_write) 

void iohandler_log(enum IOLogType type, char *text, ...);
void iohandler_events(struct IODescriptor *iofd, int readable, int writeable);
char *iohandler_iotype_name(enum IOType type);
char *iohandler_iostatus_name(enum IOStatus status);
char *iohandler_ioeventtype_name(enum IOEventType type);

#endif
