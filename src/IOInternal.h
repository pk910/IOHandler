/* IOInternal.h - IOMultiplexer v2
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
#ifndef _IOInternal_h
#define _IOInternal_h
#ifndef _IOHandler_internals
#include "IOHandler.h"
#else

/* Multithreading */
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


#define IOGC_FREE(NAME) void NAME(void *object)
typedef IOGC_FREE(iogc_free);
void iogc_add(void *object);
void iogc_add_callback(void *object, iogc_free *free_callback);

#endif
#endif
