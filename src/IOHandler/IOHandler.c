/* IOHandler.c - IOMultiplexer v2
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
#define _IOHandler_internals
#include "IOInternal.h"
#include "IOHandler.h"

#include "IOLog.h"
#include "IOGarbageCollector.h"
#include "IOTimer.h"
#include "IODNSLookup.h"
#include "IOSockets.h"

/* compat */
#include "compat/utime.h"

#define IOHANDLER_STATE_INITIALIZED  0x0001
#define IOHANDLER_STATE_RUNNING      0x0002

static int iohandler_state = 0;


void iohandler_init() {
	if((iohandler_state & IOHANDLER_STATE_INITIALIZED)) 
		return;
	
	iolog_init();
	iogc_init();
	
	_init_timers();
	_init_iodns();
	_init_sockets();
	
	iohandler_state |= IOHANDLER_STATE_INITIALIZED;
}

void iohandler_stop() {
	iohandler_state &= ~IOHANDLER_STATE_RUNNING;
}

static void iohandler_loop() {
	while(iohandler_state & IOHANDLER_STATE_RUNNING) { // endless loop
		// iohandler calls
		iogc_exec();
		iodns_poll();
		iosocket_loop(IOHANDLER_LOOP_MAXTIME);
		
	}
}

void iohandler_run() {
	iohandler_state |= IOHANDLER_STATE_RUNNING;
	
	iohandler_loop();
}

