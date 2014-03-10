/* IOSocket.cpp - IOMultiplexer v2
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
extern "C" {
	#include "../IOHandler/IOSockets.h"
}
#include "IOSocket.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

static IOSOCKET_CALLBACK(c_socket_callback) {
	CIOSocket *ciosock = (CIOSocket *) event->socket->data;
	ciosock->socket_callback(event);
}

CIOSocket::CIOSocket() {

}
CIOSocket::CIOSocket(IOSocket *iosocket) {
	this->iosocket = iosocket;
}

int CIOSocket::connect(char *hostname, unsigned int port, int ssl, char *bindhost) {
	return this->connect(hostname, port, ssl, bindhost, IOSOCKET_ADDR_IPV6 | IOSOCKET_ADDR_IPV4);
}

int CIOSocket::connect(char *hostname, unsigned int port, int ssl, char *bindhost, int flags) {
	this->iosocket = iosocket_connect_flags(hostname, port, (ssl ? 1 : 0), (bindhost ? bindhost : NULL), c_socket_callback, flags);
	if(this->iosocket) {
		this->iosocket->data = this;
		return 1;
	} else
		return 0;
}

int CIOSocket::listen(char *hostname, unsigned int port) {
	return this->listen(hostname, port, IOSOCKET_ADDR_IPV6 | IOSOCKET_ADDR_IPV4);
}
int CIOSocket::listen(char *hostname, unsigned int port, int flags) {
	this->iosocket = iosocket_listen_flags(hostname, port, c_socket_callback, flags);
	if(this->iosocket) {
		this->iosocket->data = this;
		return 1;
	} else
		return 0;
}

int CIOSocket::listen_ssl(char *hostname, unsigned int port, char *certfile, char *keyfile) {
	return listen_ssl(hostname, port, certfile, keyfile, IOSOCKET_ADDR_IPV6 | IOSOCKET_ADDR_IPV4);
}
int CIOSocket::listen_ssl(char *hostname, unsigned int port, char *certfile, char *keyfile, int flags) {
	this->iosocket = iosocket_listen_ssl_flags(hostname, port, certfile, keyfile, c_socket_callback, flags);
	if(this->iosocket) {
		this->iosocket->data = this;
		return 1;
	} else
		return 0;
}

void CIOSocket::write(const char *data, int len) {
	iosocket_send(iosocket, data, len);
}
#define IOSOCKET_PRINTF_LEN 2048
void CIOSocket::writef(const char *format, ...) {
	va_list arg_list;
	char sendBuf[IOSOCKET_PRINTF_LEN];
	int pos;
	sendBuf[0] = '\0';
	va_start(arg_list, format);
	pos = vsnprintf(sendBuf, IOSOCKET_PRINTF_LEN - 1, format, arg_list);
	va_end(arg_list);
	if (pos < 0 || pos > (IOSOCKET_PRINTF_LEN - 1)) pos = IOSOCKET_PRINTF_LEN - 1;
	sendBuf[pos] = '\0';
	iosocket_send(iosocket, sendBuf, pos);
}

void CIOSocket::close() {
	iosocket_close(iosocket);
};


void CIOSocket::socket_callback(IOSocketEvent *event) {
	switch(event->type) {
	case IOSOCKETEVENT_RECV:
		if(iosocket->parse_delimiter)
			this->recvLine(event->data.recv_str);
		else {
			IOSocketBuffer *recvbuf = event->data.recv_buf;
			int usedlen;
			usedlen = this->recvEvent(recvbuf->buffer, recvbuf->bufpos);
			if(usedlen == recvbuf->bufpos) {
				recvbuf->bufpos = 0;
			} else {
				memmove(recvbuf->buffer, recvbuf->buffer + usedlen, recvbuf->bufpos - usedlen);
				recvbuf->bufpos -= usedlen;
			}
		}
		break;
    case IOSOCKETEVENT_CONNECTED:
		this->connectedEvent();
		break;
	case IOSOCKETEVENT_NOTCONNECTED:
		this->notConnectedEvent(event->data.errid);
		break;
	case IOSOCKETEVENT_CLOSED:
		this->closedEvent(event->data.errid);
		break;
	case IOSOCKETEVENT_ACCEPT:
		this->acceptedEvent(new CIOSocket(event->data.accept_socket));
		break;
	case IOSOCKETEVENT_DNSFAILED:
		this->dnsErrEvent(event->data.recv_str);
		break;
	}
}

int CIOSocket::getSSL() {
	
}
int CIOSocket::getIPv6() {
	
}
int CIOSocket::getConnected() {
	
}
int CIOSocket::getListening() {
	
}
