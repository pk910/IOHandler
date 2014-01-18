/* IOTimer.h - IOMultiplexer v2
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
#ifndef _IOTimer_h
#define _IOTimer_h
#ifndef _IOHandler_internals
#include "IOHandler.h"
#else

#define IOTIMERFLAG_PERIODIC       0x01
#define IOTIMERFLAG_ACTIVE         0x02
#define IOTIMERFLAG_IN_LIST        0x04
#define IOTIMERFLAG_PARENT_PUBLIC  0x08
#define IOTIMERFLAG_PARENT_SOCKET  0x10

struct _IOTimerDescriptor;

extern struct _IOTimerDescriptor *iotimer_sorted_descriptors;

struct _IOTimerDescriptor {
    unsigned int flags : 8;
    void *parent; 
    
    struct timeval timeout;
    struct timeval autoreload;
    
    struct _IOTimerDescriptor *prev, *next;
};

void _init_timers();
struct _IOTimerDescriptor _create_timer(struct timeval timeout);
void _destroy_timer(struct _IOTimerDescriptor *timer);

#endif

#define IOTIMER_CALLBACK(NAME) void NAME(struct IOTimerDescriptor *iotimer)
typedef IOTIMER_CALLBACK(iotimer_callback);

struct IOTimerDescriptor {
    void *iotimer; /* struct _IOTimerDescriptor */
    
    iotimer_callback *callback;
    void *data;
};

struct IOTimerDescriptor iotimer_create(struct timeval *timeout);
void iotimer_start(struct IOTimerDescriptor *iotimer);
void iotimer_set_autoreload(struct IOTimerDescriptor *iotimer, struct timeval *autoreload);
void iotimer_set_callback(struct IOTimerDescriptor *iotimer, iotimer_callback *callback);
void iotimer_destroy(struct IOTimerDescriptor *iotimer);

#endif
