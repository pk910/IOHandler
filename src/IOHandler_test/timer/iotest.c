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
#include <sys/time.h>
#include "../../IOHandler/IOHandler.h"
#include "../../IOHandler/IOTimer.h"
#include "../../IOHandler/IOLog.h"

#define TEST_DURATION 100

static IOTIMER_CALLBACK(io_callback);
static IOLOG_CALLBACK(io_log);

static struct timeval test_clock1, test_clock2;
static int timercount;

int main(int argc, char *argv[]) {
	iohandler_init();
	iolog_register_callback(io_log);
	
	gettimeofday(&test_clock1, NULL);
	gettimeofday(&test_clock2, NULL);

	struct timeval tv;
	tv.tv_sec = TEST_DURATION / 1000;
	tv.tv_usec = TEST_DURATION % 1000 * 1000;
	struct IOTimerDescriptor *timer = iotimer_create(NULL);
	iotimer_set_callback(timer, io_callback);
	iotimer_set_autoreload(timer, &tv);
	iotimer_start(timer);
	
	timercount = 0;
	
	printf("[timer 0] %ld.%ld\n", test_clock1.tv_sec, test_clock1.tv_usec);
	
	iohandler_run();
	return 1;
}

static IOTIMER_CALLBACK(io_callback) {
	struct timeval curr_time;
	int diff1;
	double diff2;
	
	timercount++;
	gettimeofday(&curr_time, NULL);
	diff1 = (curr_time.tv_sec - test_clock1.tv_sec) * 1000 + ((curr_time.tv_usec - test_clock1.tv_usec) / 1000);
	diff2 = (curr_time.tv_sec - test_clock2.tv_sec) * 1000 + ((curr_time.tv_usec - test_clock2.tv_usec) / 1000.0);
	diff2 -= (timercount * TEST_DURATION);
	gettimeofday(&test_clock1, NULL);
	printf("[timer %03d] %ld.%06ld [%d ms]  accuracy: %f ms\n", timercount, curr_time.tv_sec, curr_time.tv_usec, diff1, diff2);
}

static IOLOG_CALLBACK(io_log) {
	//printf("%s", line);
}
