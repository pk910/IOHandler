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
#include "../../IOHandler++/IOHandler.h"
#include "../../IOHandler++/IOSocket.h"

class IOTestSocket : public CIOSocket {
protected:
	virtual void connectedEvent() {
		printf("[connect]\n");
		this->writef("GET / HTTP/1.1\r\n");
		this->writef("Host: test.pk910.de\r\n");
		this->writef("\r\n");
	};
	virtual void notConnectedEvent(int errid) {
		printf("[not connected]\n");
	};
	virtual void closedEvent(int errid) {
		printf("[disconnect]\n");
	};
	virtual void acceptedEvent(CIOSocket *client) {
		client->close(); 
	};
	virtual void dnsErrEvent(std::string *errormsg) {
	
	};
	virtual int recvEvent(const char *data, int len) {
		int i;
		for(i = 0; i < len; i++)
			putchar(data[i]);
		return len;
	};
};


int main(int argc, char *argv[]) {
	CIOHandler *iohandler = new CIOHandler();
	IOTestSocket *sock = new IOTestSocket();
	sock->connect("test.pk910.de", 443, 1, NULL);
	
	iohandler->start();
}
