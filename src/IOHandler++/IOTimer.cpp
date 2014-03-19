/* IOTimer.cpp - IOMultiplexer v2
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
	#include "../IOHandler/IOTimer.h"
}
#include "IOTimer.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

static IOTIMER_CALLBACK(c_timer_callback) {
	CIOTimer *ciotimer = (CIOTimer *) iotimer->data;
	ciotimer->timer_callback();
}

CIOTimer::CIOTimer() {
	this->iotimer = iotimer_create(NULL);
	this->iotimer->data = this;
	iotimer_set_callback(this->iotimer, c_timer_callback);
	iotimer_set_persistent(this->iotimer, 1);
}

CIOTimer::~CIOTimer() {
	iotimer_destroy(this->iotimer);
}

void CIOTimer::setTimeout(timeval timeout) {
	iotimer_set_timeout(this->iotimer, &timeout);
}

void CIOTimer::setRelativeTimeout(timeval timeout) {
	timeval now;
	gettimeofday(&now, NULL);
	
	timeout.tv_sec += now.tv_sec;
	timeout.tv_usec += now.tv_usec;
	while(timeout.tv_usec > 1000000) {
		timeout.tv_usec -= 1000000;
		timeout.tv_sec++;
	}
	
	this->setTimeout(timeout);
}

void CIOTimer::setRelativeTimeout(timeval timeout, int auto_reload) {
	this->setRelativeTimeout(timeout);
	if(auto_reload)
		this->setAutoReload(timeout);
}

void CIOTimer::setRelativeTimeoutSeconds(double seconds) {
	this->setRelativeTimeoutSeconds(seconds, 0);
}

void CIOTimer::setRelativeTimeoutSeconds(double seconds, int auto_reload) {
	timeval tout;
	tout.tv_sec = (int) seconds;
	tout.tv_usec = ((int) (seconds * 1000000)) % 1000000;
	this->setRelativeTimeout(tout);
	if(auto_reload)
		this->setAutoReload(tout);
}

timeval CIOTimer::getTimeout() {
	return iotimer_get_timeout(this->iotimer);
}

timeval CIOTimer::getRelativeTimeout() {
	timeval tout, now;
	gettimeofday(&now, NULL);
	
	tout = iotimer_get_timeout(this->iotimer);
	
	if(tout.tv_sec || tout.tv_usec) {
		tout.tv_sec = tout.tv_sec - now.tv_sec;
		tout.tv_usec = tout.tv_usec - now.tv_usec;
		if(tout.tv_usec < 0) {
			tout.tv_usec += 1000000;
			tout.tv_sec--;
		}
	}
	
	return tout;
}

double CIOTimer::getRelativeTimeoutSeconds() {
	timeval tout = this->getRelativeTimeout();
	return tout.tv_sec + (tout.tv_usec / 1000000);
}


void CIOTimer::setAutoReload(timeval timeout) {
	iotimer_set_autoreload(this->iotimer, &timeout);
}

void CIOTimer::clearAutoReload() {
	iotimer_set_autoreload(this->iotimer, NULL);
}

timeval CIOTimer::getAutoReloadTime() {
	return iotimer_get_autoreload(this->iotimer);
}

int CIOTimer::getAutoReloadState() {
	timeval timeout = this->getAutoReloadTime();
	if(timeout.tv_sec == 0 && timeout.tv_usec == 0)
		return 0;
	else
		return 1;
}

void CIOTimer::setActive(int active) {
	if(active)
		iotimer_start(this->iotimer);
	else
		iotimer_stop(this->iotimer);
}

int CIOTimer::getActive() {
	return iotimer_state(this->iotimer);
}

void CIOTimer::timer_callback() {
	this->timeout();
}

