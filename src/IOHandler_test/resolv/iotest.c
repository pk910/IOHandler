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
#include "../../IOHandler/IODNSLookup.h"
#include "../../IOHandler/IOLog.h"

#ifndef WIN32
#include "arpa/inet.h"
#endif
#include "../../IOHandler/compat/inet.h"

#define TEST_DURATION 100

static IODNS_CALLBACK(io_callback);
static IOLOG_CALLBACK(io_log);

int main(int argc, char *argv[]) {
	iohandler_init();
	iolog_register_callback(io_log);
	
	iodns_getaddrinfo("google.de", (IODNS_RECORD_A | IODNS_RECORD_AAAA), io_callback, "01");
	iodns_getaddrinfo("pk910.de", IODNS_RECORD_AAAA, io_callback, "02");
	iodns_getaddrinfo("nonexisting.no-tld", (IODNS_RECORD_A | IODNS_RECORD_AAAA), io_callback, "03");
	iodns_getaddrinfo("google.com", (IODNS_RECORD_A | IODNS_RECORD_AAAA), io_callback, "04");
	iodns_getaddrinfo("test.pk910.de", IODNS_RECORD_A, io_callback, "05");
	
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);
	iodns_getnameinfo((struct sockaddr *)&addr, sizeof(addr), io_callback, "06");
	
	iohandler_run();
	return 1;
}

static IODNS_CALLBACK(io_callback) {
	struct IODNSQuery *iodns = event->query;
	char *id = iodns->data;
	if(event->type == IODNSEVENT_SUCCESS) {
		printf("Query %s succeeded:\n", id);
		struct IODNSResult *result;
		char str[1024];
		for(result = event->result; result; result = result->next) {
			switch(result->type) {
			case IODNS_RECORD_A:
				inet_ntop(AF_INET, &((struct sockaddr_in *)result->result.addr.address)->sin_addr, str, INET_ADDRSTRLEN);
				printf("  A: %s\n", str);
				
				if(!strcmp(id, "05"))
					iodns_getnameinfo(result->result.addr.address, result->result.addr.addresslen, io_callback, "07");
				break;
			case IODNS_RECORD_AAAA:
				inet_ntop(AF_INET6, &((struct sockaddr_in6 *)result->result.addr.address)->sin6_addr, str, INET6_ADDRSTRLEN);
				printf("  AAAA: %s\n", str);
				
				if(!strcmp(id, "05"))
					iodns_getnameinfo(result->result.addr.address, result->result.addr.addresslen, io_callback, "07");
				break;
			case IODNS_RECORD_PTR:
				printf("  PTR: %s\n", result->result.host);
				break;
			}
		}
		iodns_free_result(event->result);
	} else
		printf("Query %s failed.\n", id);
}

static IOLOG_CALLBACK(io_log) {
	//printf("%s", line);
}
