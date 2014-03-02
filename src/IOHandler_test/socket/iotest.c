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
#include "../../IOHandler/IOHandler.h"
#include "../../IOHandler/IOSockets.h"
#include "../../IOHandler/IOLog.h"

static IOSOCKET_CALLBACK(io_callback);
static IOLOG_CALLBACK(io_log);

static struct IOSocket *irc_iofd = NULL;

int main(int argc, char *argv[]) {
	iohandler_init();
	
    iolog_register_callback(io_log);
    
    irc_iofd = iosocket_connect_flags("irc.nextirc.net", 6667, 0, NULL, io_callback, IOSOCKET_ADDR_IPV4);
    irc_iofd->parse_delimiter = 1;
	irc_iofd->delimiters[0] = '\n';
	irc_iofd->delimiters[1] = '\r';
	
	iohandler_run();
	
	return 0;
}

static IOSOCKET_CALLBACK(io_callback) {
    switch(event->type) {
        case IOSOCKETEVENT_CONNECTED:
            printf("[connect]\n");
            break;
        case IOSOCKETEVENT_CLOSED:
            printf("[disconnect]\n");
            break;
        case IOSOCKETEVENT_RECV:
            printf("[in] %s\n", event->data.recv_str);
            break;
        
        default:
            break;
    }
}

static IOLOG_CALLBACK(io_log) {
    //printf("%s", line);
}
