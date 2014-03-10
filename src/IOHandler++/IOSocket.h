/* IOSocket.h - IOMultiplexer v2
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
#ifndef _IOSocket_cpp_h
#define _IOSocket_cpp_h

extern "C" {
	#define IOSOCKET_CPP
	#include "../IOHandler/IOSockets.h"
}
#include <iostream>
#include <string>

struct IOSocket;

class CIOSocket {
public:
	CIOSocket();
	
	int connect(char *hostname, unsigned int port, int ssl, char *bindhost);
	int connect(char *hostname, unsigned int port, int ssl, char *bindhost, int flags);
	
	int listen(char *hostname, unsigned int port);
	int listen(char *hostname, unsigned int port, int flags);
	int listen_ssl(char *hostname, unsigned int port, char *certfile, char *keyfile);
	int listen_ssl(char *hostname, unsigned int port, char *certfile, char *keyfile, int flags);
	
	void write(const char *data, int len);
	void writef(const char *format, ...);
	
	CIOSocket accept();
	
	void close();
	
	
	int getSSL();
	int getIPv6();
	int getConnected();
	int getListening();
	
	
	void socket_callback(IOSocketEvent *event);
protected:
	virtual int recvEvent(const char *data, int len) { return len; };
	virtual void recvLine(char *line) {};
	void enableRecvLine();
	void disableRecvLine();
	
	virtual void connectedEvent() {};
	virtual void notConnectedEvent(int errid) {};
	virtual void closedEvent(int errid) {};
	virtual void acceptedEvent(CIOSocket *client) { client->close(); };
	virtual void dnsErrEvent(char *errormsg) {};
	
private:
	IOSocket *iosocket;
	
	CIOSocket(IOSocket *iosocket);
};

#endif
