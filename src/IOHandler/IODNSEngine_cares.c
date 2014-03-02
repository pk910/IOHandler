/* IODNSEngine_cares.c - IOMultiplexer
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
#include "IODNSLookup.h"

static int dnsengine_cares_init() {
	/* TODO */
    return 0;
}

static void dnsengine_cares_stop() {
	/* TODO */
}

static void dnsengine_cares_add(struct _IODNSQuery *iodns) {
    /* TODO */
}

static void dnsengine_cares_remove(struct _IODNSQuery *iodns) {
    /* TODO */
}

static void dnsengine_cares_loop() {
    /* TODO */
}

struct IODNSEngine dnsengine_cares = {
    .name = "c-ares",
    .init = dnsengine_cares_init,
	.stop = dnsengine_cares_stop,
    .add = dnsengine_cares_add,
    .remove = dnsengine_cares_remove,
    .loop = dnsengine_cares_loop,
};
