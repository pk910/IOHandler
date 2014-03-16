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
#include <string.h>
#include "../../IOHandler/IOHandler.h"
#include "../../IOHandler/IOSockets.h"
#include "../../IOHandler/IOLog.h"

#define CERTFILE "cert.pem"
#define KEYFILE "key.pen"

static IOSOCKET_CALLBACK(io_callback);
static IOLOG_CALLBACK(io_log);

static struct IOSocket *irc_iofd = NULL;

int main(int argc, char *argv[]) {
	iohandler_init();
	
    iolog_register_callback(io_log);
    
    irc_iofd = iosocket_listen_ssl("0.0.0.0", 12345, CERTFILE, KEYFILE, io_callback);
	
	iohandler_run();
	
	return 0;
}

static IOSOCKET_CALLBACK(io_callback) {
    switch(event->type) {
        case IOSOCKETEVENT_ACCEPT:
            printf("[client accepted]\n");
			struct IOSocket *client = event->data.accept_socket;
			client->callback = io_callback;
			
			char *html = "<html><head><title>Test Page</title></head><body><h1>IOHandler SSL Test</h1></body></html>";
            iosocket_printf(client, "HTTP/1.1 200 OK\r\n");
			iosocket_printf(client, "Server: Apache\r\n");
			iosocket_printf(client, "Content-Length: %d\r\n", strlen(html));
			iosocket_printf(client, "Content-Type: text/html\r\n");
			iosocket_printf(client, "\r\n");
			iosocket_printf(client, "%s", html);
			
            break;
        case IOSOCKETEVENT_CLOSED:
			if(event->socket->listening)
				printf("[server closed]\n");
			else
				printf("[client disconnect]\n");
            break;
        case IOSOCKETEVENT_RECV:
			{
				struct IOSocketBuffer *recv_buf = event->data.recv_buf;
				int i;
				for(i = 0; i < recv_buf->bufpos; i++)
					putchar(recv_buf->buffer[i]);
				recv_buf->bufpos = 0;
				printf("\n");
            }
            break;
        
        default:
            break;
    }
}

static IOLOG_CALLBACK(io_log) {
	printf("%s", message);
}
