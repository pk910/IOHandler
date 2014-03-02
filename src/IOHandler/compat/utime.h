/* utime.h - IOMultiplexer
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
#ifndef _compat_utime_h
#define _compat_utime_h
#include "../IOHandler_config.h"
#include <sys/time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

void usleep_tv(struct timeval tv);

#ifndef HAVE_USLEEP
void usleep(long usec);
#endif
#endif
