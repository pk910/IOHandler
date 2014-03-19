/* IOTimer.h - IOMultiplexer v2
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
#ifndef _IOTimer_cpp_h
#define _IOTimer_cpp_h

#include <iostream>
#include <string>
#include <sys/time.h>

struct IOTimerDescriptor;

class CIOTimer {
public:
	CIOTimer();
	~CIOTimer();
	
	void setTimeout(timeval timeout);
	timeval getTimeout();
	void setRelativeTimeout(timeval timeout);
	void setRelativeTimeout(timeval timeout, int auto_reload);
	timeval getRelativeTimeout();
	void setRelativeTimeoutSeconds(double seconds);
	void setRelativeTimeoutSeconds(double seconds, int auto_reload);
	double getRelativeTimeoutSeconds();
	
	void setAutoReload(timeval timeout);
	void clearAutoReload();
	int getAutoReloadState();
	timeval getAutoReloadTime();
	
	void setActive(int active);
	int getActive();
	
	
	void timer_callback();
protected:
	virtual void timeout() {};
	
private:
	IOTimerDescriptor *iotimer;
};

#endif
