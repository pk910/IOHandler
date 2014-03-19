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
#include "../../IOHandler++/IOTimer.h"

class IOTestTimer : public CIOTimer {
public:
	IOTestTimer(int test_duration) : CIOTimer() {
		this->tick_count = 0;
		this->test_duration = test_duration;
		gettimeofday(&this->test_clock1, NULL);
		gettimeofday(&this->test_clock2, NULL);
		
		this->setRelativeTimeoutSeconds(this->test_duration / 1000.0, 1);
		this->setActive(1);
		
		printf("[timer 0] %ld.%ld\n", test_clock1.tv_sec, test_clock1.tv_usec);
	};
	
protected:
	virtual void timeout() {
		this->tick_count++;
		this->tick();
	};
	
private:
	timeval test_clock1, test_clock2;
	int tick_count, test_duration;
	
	void tick() {
		timeval curr_time;
		int diff1;
		double diff2;
		
		gettimeofday(&curr_time, NULL);
		diff1 = (curr_time.tv_sec - this->test_clock1.tv_sec) * 1000 + ((curr_time.tv_usec - this->test_clock1.tv_usec) / 1000);
		diff2 = (curr_time.tv_sec - this->test_clock2.tv_sec) * 1000 + ((curr_time.tv_usec - this->test_clock2.tv_usec) / 1000.0);
		diff2 -= (this->tick_count * this->test_duration);
		gettimeofday(&this->test_clock1, NULL);
		printf("[timer %03d] %ld.%06ld [%d ms]  accuracy: %f ms\n", this->tick_count, curr_time.tv_sec, curr_time.tv_usec, diff1, diff2);
	};
};


int main(int argc, char *argv[]) {
	CIOHandler *iohandler = new CIOHandler();
	IOTestTimer *timer = new IOTestTimer(100);
	
	iohandler->start();
}
