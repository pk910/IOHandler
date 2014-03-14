/* IOLog.c - IOMultiplexer v2
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct iolog_callback_entry {
	iolog_callback *callback;
	struct iolog_callback_entry *next;
};
static struct iolog_callback_entry *iolog_callbacks = NULL;

void iolog_init() {

}

#define MAXLOG 1024

void iolog_trigger(enum IOLogType type, char *text, ...) {
    va_list arg_list;
    char logBuf[MAXLOG+1];
    int pos;
    logBuf[0] = '\0';
    va_start(arg_list, text);
    pos = vsnprintf(logBuf, MAXLOG - 1, text, arg_list);
    va_end(arg_list);
    if (pos < 0 || pos > (MAXLOG - 1)) pos = MAXLOG - 1;
    logBuf[pos] = '\n';
    logBuf[pos+1] = '\0';
    
    struct iolog_callback_entry *callback;
	for(callback = iolog_callbacks; callback; callback = callback->next)
		callback->callback(type, logBuf);
}

void iolog_register_callback(iolog_callback *callback) {
	struct iolog_callback_entry *logcb = malloc(sizeof(*logcb));
	if(!logcb) {
		iolog_trigger(IOLOG_ERROR, "Failed to allocate memory for iolog_callback_entry in %s:%d", __FILE__, __LINE__);
		return;
	}
	logcb->callback = callback;
	logcb->next = iolog_callbacks;
	iolog_callbacks = logcb;
}
