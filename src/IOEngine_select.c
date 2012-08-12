/* IOEngine_select.c - IOMultiplexer
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
#include <errno.h>
#ifdef WIN32
#define _WIN32_WINNT 0x501
#include <windows.h>
#include <winsock2.h>
#else
#include <string.h>
#include <stdio.h>
#endif

static int engine_select_init() {
    /* empty */
    return 1;
}

static void engine_select_add(struct IODescriptor *iofd) {
    #ifdef WIN32
    if(iofd->type == IOTYPE_STDIN)
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT);
    #endif
    /* empty */
}

static void engine_select_remove(struct IODescriptor *iofd) {
    /* empty */
}

static void engine_select_update(struct IODescriptor *iofd) {
    /* empty */
}

static void engine_select_loop(struct timeval *timeout) {
    fd_set read_fds;
    fd_set write_fds;
    unsigned int fds_size = 0;
    struct IODescriptor *iofd, *tmp_iofd;
    struct timeval now, tdiff;
    int select_result;
    
    gettimeofday(&now, NULL);
    
    //clear fds
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    
    for(iofd = first_descriptor; iofd; iofd = tmp_iofd) {
        tmp_iofd = iofd->next;
        if(iofd->type == IOTYPE_STDIN) {
            #ifdef WIN32
            //WIN32 doesn't support stdin within select
            //just try to read the single events from the console
            DWORD dwRead;
            INPUT_RECORD inRecords[128];
            unsigned int i;
            int read_bytes = 0;
            GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &dwRead);
            if(dwRead)
                ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &inRecords[0], 128, &dwRead);
            for (i = 0; i < dwRead; ++i) {
                if (inRecords[i].EventType == KEY_EVENT) {
                    const char c = inRecords[i].Event.KeyEvent.uChar.AsciiChar;
                    if (inRecords[i].Event.KeyEvent.bKeyDown && c != 0) {
                        iofd->readbuf.buffer[iofd->readbuf.bufpos + read_bytes] = c;
                        read_bytes++;
                    }
                }
            }
            if(read_bytes)
                iohandler_events(iofd, read_bytes, 0);
            if(read_bytes >= 128) {
                timeout->tv_sec = 0;
                timeout->tv_usec = 1;
                //minimal timeout
            } else {
                timeout->tv_sec = 0;
                timeout->tv_usec = 100000;
            }
            #else
            if(iofd->fd > fds_size)
                fds_size = iofd->fd;
            FD_SET(iofd->fd, &read_fds);
            #endif
        }
        else if(iofd->type == IOTYPE_SERVER || iofd->type == IOTYPE_CLIENT) {
            if(iofd->fd > fds_size)
                fds_size = iofd->fd;
            FD_SET(iofd->fd, &read_fds);
            if(iohandler_wants_writes(iofd))
                FD_SET(iofd->fd, &write_fds);
        }
    }
    
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
    
    //select system call
    select_result = select(fds_size + 1, &read_fds, &write_fds, NULL, timeout);
    
    if (select_result < 0) {
        if (errno != EINTR) {
            iohandler_log(IOLOG_FATAL, "select() failed with errno %d %d: %s", select_result, errno, strerror(errno));
            return;
        }
    }
    
    gettimeofday(&now, NULL);
    
    //check all descriptors
    for(iofd = first_descriptor; iofd; iofd = tmp_iofd) {
        tmp_iofd = iofd->next;
        if(iofd->type == IOTYPE_SERVER || iofd->type == IOTYPE_CLIENT || iofd->type == IOTYPE_STDIN) {
            if(FD_ISSET(iofd->fd, &read_fds) || FD_ISSET(iofd->fd, &write_fds)) {
                iohandler_events(iofd, FD_ISSET(iofd->fd, &read_fds), FD_ISSET(iofd->fd, &write_fds));
                continue;
            }
        }
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
