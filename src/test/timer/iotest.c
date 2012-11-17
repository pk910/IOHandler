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
#include "../../IOHandler.h"

#define TEST_DURATION 100

static IOHANDLER_CALLBACK(io_callback);
static IOHANDLER_LOG_BACKEND(io_log);

static struct timeval test_clock1, test_clock2;
static int timercount;

void add_timer(int ms) {
    struct timeval timeout;
    gettimeofday(&timeout, NULL);
    timeout.tv_usec += (ms % 1000) * 1000;
    timeout.tv_sec += (ms / 1000);
    if(timeout.tv_usec > 1000000) {
        timeout.tv_usec -= 1000000;
        timeout.tv_sec++;
    }
    iohandler_timer(timeout, io_callback);
}

int main(int argc, char *argv[]) {
    iolog_backend = io_log;
    
    iohandler_set(IOHANDLER_SETTING_HIGH_PRECISION_TIMER, 1);
    
    gettimeofday(&test_clock1, NULL);
    gettimeofday(&test_clock2, NULL);
    //add_timer(TEST_DURATION);
    iohandler_constant_timer(TEST_DURATION, io_callback);
    timercount = 0;
    
    printf("[timer 0] %ld.%ld\n", test_clock1.tv_sec, test_clock1.tv_usec);
    
    while(1) {
        iohandler_poll();
    }
}

static IOHANDLER_CALLBACK(io_callback) {
    struct timeval curr_time;
    int diff1;
    double diff2;
    switch(event->type) {
        case IOEVENT_TIMEOUT:
            //add_timer(TEST_DURATION);
            timercount++;
            gettimeofday(&curr_time, NULL);
            diff1 = (curr_time.tv_sec - test_clock1.tv_sec) * 1000 + ((curr_time.tv_usec - test_clock1.tv_usec) / 1000);
            diff2 = (curr_time.tv_sec - test_clock2.tv_sec) * 1000 + ((curr_time.tv_usec - test_clock2.tv_usec) / 1000.0);
            diff2 -= (timercount * TEST_DURATION);
            gettimeofday(&test_clock1, NULL);
            printf("[timer %03d] %ld.%06ld [%d ms]  accuracy: %f ms\n", timercount, curr_time.tv_sec, curr_time.tv_usec, diff1, diff2);
            break;
        default:
            break;
    }
}

static IOHANDLER_LOG_BACKEND(io_log) {
    //printf("%s", line);
}
