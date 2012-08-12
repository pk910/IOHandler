/* main.c - IOMultiplexer
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

#include <stdio.h>
#include "IOHandler.h"

static IOHANDLER_CALLBACK(io_callback);
static IOHANDLER_LOG_BACKEND(io_log);

int main(int argc, char *argv[]) {
    iolog_backend = io_log;
    
    iohandler_connect("pk910.de", 6667, 0, NULL, io_callback);
    
    struct IODescriptor *iofd;
    iofd = iohandler_add(0, IOTYPE_STDIN, NULL, io_callback);
    iofd->read_lines = 1;
    
    while(1) {
        iohandler_poll();
    }
}

static IOHANDLER_CALLBACK(io_callback) {
    switch(event->type) {
        case IOEVENT_CONNECTED:
            printf("[connect]\n");
            break;
        case IOEVENT_RECV:
            printf("[in] %s\n", event->data.recv_str);
            break;
        default:
            break;
    }
}

static IOHANDLER_LOG_BACKEND(io_log) {
    printf("%s\n", line);
}
