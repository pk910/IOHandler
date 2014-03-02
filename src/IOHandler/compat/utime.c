/* utime.c - IOMultiplexer
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
#include "../IOHandler_config.h"
#include "utime.h"
#ifndef HAVE_USLEEP

#ifdef HAVE_SELECT

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#else
# include <sys/time.h>
# include <sys/types.h>
# include <unistd.h>
#endif

void usleep_tv(struct timeval tv) {
	select(0, NULL, NULL, NULL, &tv);
}

void usleep(long usec)
{
	struct timeval tv;

	tv.tv_sec = usec / 1000000;
	tv.tv_usec = usec % 1000000;
	usleep_tv(tv);
}

#elif defined WIN32

/* usleep implementation from FreeSCI */

#include <windows.h>

void usleep_tv(struct timeval tv) {
	usleep(tv.tv_sec * 1000000 + tv.tv_usec);
}

void usleep (long usec)
{
        LARGE_INTEGER lFrequency;
        LARGE_INTEGER lEndTime;
        LARGE_INTEGER lCurTime;

        QueryPerformanceFrequency (&lFrequency);
        if (lFrequency.QuadPart) {
                QueryPerformanceCounter (&lEndTime);
                lEndTime.QuadPart += (LONGLONG) usec *
                                        lFrequency.QuadPart / 1000000;
                do {
                        QueryPerformanceCounter (&lCurTime);
                        Sleep(0);
                } while (lCurTime.QuadPart < lEndTime.QuadPart);
        }
}

#endif

#else
#include <sys/time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

void usleep_tv(struct timeval tv) {
	usleep(tv.tv_sec * 1000000 + tv.tv_usec);
}

#endif /* !HAVE_USLEEP */
