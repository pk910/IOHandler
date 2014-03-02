/* IOEngine_win32.c - IOMultiplexer
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
#include "IOSockets.h"
#include "IOTimer.h"

#ifdef WIN32

#define _WIN32_WINNT 0x501
#include <windows.h>
#include <winsock2.h>

/* This is massively kludgy.  Unfortunately, the only performant I/O
 * multiplexer with halfway decent semantics under Windows is
 * WSAAsyncSelect() -- which requires a window that can receive
 * messages.
 *
 * So ioset_win32_init() creates a hidden window and sets it up for
 * asynchronous socket notifications.
 */

#define IDT_TIMER1 1000
#define IDT_TIMER2 1001
#define IDT_SOCKET 1002

static HWND ioset_window;

static struct _IOSocket *engine_win32_get_iosock(int fd) {
	struct _IOSocket *iosock;
	for(iosock = iosocket_first; iosock; iosock = iosock->next) {
		if(iosock->fd == fd)
			return iosock;
	}
	return NULL;
}

static LRESULT CALLBACK engine_win32_wndproc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	struct _IOSocket *iosock;
	int events;

	if (hWnd == ioset_window) {
		switch (uMsg) {
		case IDT_TIMER1:
			return 0;
		case IDT_TIMER2:
			//check timers
			_trigger_timer();
			return 0;
		case IDT_SOCKET:
			iosock = engine_win32_get_iosock(wParam);
			events = WSAGETSELECTEVENT(lParam);
			
			iosocket_events_callback(iosock, (events & (FD_READ | FD_ACCEPT | FD_CLOSE)) != 0, (events & (FD_WRITE | FD_CONNECT)) != 0);
			return 0;
		case WM_QUIT:
			return 0;
		}
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static int engine_win32_init() {
	WNDCLASSEX wcx;
	HINSTANCE hinst;
	WSADATA wsadata;
	
	// Start Windows Sockets.
	if (WSAStartup(MAKEWORD(2, 0), &wsadata)) {
		iolog_trigger(IOLOG_FATAL, "Unable to start Windows Sockets");
		return 0;
	}
	
	// Get Windows HINSTANCE.
	hinst = GetModuleHandle(NULL);

	// Describe and register a window class.
	memset(&wcx, 0, sizeof(wcx));
	wcx.cbSize = sizeof(wcx);
	wcx.lpfnWndProc = engine_win32_wndproc;
	wcx.hInstance = hinst;
	wcx.lpszClassName = "IOMultiplexerMainWindow";
	if (!RegisterClassEx(&wcx))
		return 0;

	ioset_window = CreateWindow("IOMultiplexerMainWindow", "IOMultiplexer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hinst, NULL);
	if (!ioset_window)
		return 0;
	return 1;
}

static long engine_win32_events(struct _IOSocket *iosock) {
	if(iosock->socket_flags & IOSOCKETFLAG_LISTENING)
		return FD_ACCEPT;
	if(iosock->socket_flags & IOSOCKETFLAG_CONNECTING)
		return FD_CONNECT;
	
	return FD_READ | FD_CLOSE | (iosocket_wants_writes(iosock) ? FD_WRITE : 0);
}

static void engine_win32_update(struct _IOSocket *iosock) {
	long events;
	events = engine_win32_events(iosock);
	WSAAsyncSelect(iosock->fd, ioset_window, IDT_SOCKET, events);
}

static void engine_win32_add(struct _IOSocket *iosock) {
	engine_win32_update(iosock);
}

static void engine_win32_remove(struct _IOSocket *iosock) {
	unsigned long ulong = 0;
	WSAAsyncSelect(iosock->fd, ioset_window, IDT_SOCKET, 0);
	ioctlsocket(iosock->fd, FIONBIO, &ulong);
}

static void engine_win32_loop(struct timeval *timeout) {
	MSG msg;
	BOOL res;
	int msec, msec2;
	struct timeval now;
	
	//check timers
	gettimeofday(&now, NULL);
	if(iotimer_sorted_descriptors && timeval_is_bigger(now, iotimer_sorted_descriptors->timeout))
		_trigger_timer();
	
	//get timeout (timer or given timeout)
	if(iotimer_sorted_descriptors) {
		msec = (iotimer_sorted_descriptors->timeout.tv_sec - now.tv_sec) * 1000;
		msec += (iotimer_sorted_descriptors->timeout.tv_usec - now.tv_usec) / 1000;
	}
	if(timeout) {
		msec2 = (timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
		if(!iotimer_sorted_descriptors || msec2 < msec)
			msec = msec2;
	} else if(!iotimer_sorted_descriptors)
		msec = -1;
	
	//set TIMER
	SetTimer(ioset_window, IDT_TIMER1, 1000, NULL);
	if(msec > -1)
		SetTimer(ioset_window, IDT_TIMER2, msec, NULL);
	
	//GetMessage system call
	res = GetMessage(&msg, NULL, 0, 0);
	
	//kill TIMER
	KillTimer(ioset_window, IDT_TIMER1);
	if(msec > -1)
		KillTimer(ioset_window, IDT_TIMER2);
	
	if (res <=0)
		return;
	else {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static void engine_win32_cleanup() {
	DestroyWindow(ioset_window);
	ioset_window = NULL;
	WSACleanup();
}

struct IOEngine engine_win32 = {
	.name = "win32",
	.init = engine_win32_init,
	.add = engine_win32_add,
	.remove = engine_win32_remove,
	.update = engine_win32_update,
	.loop = engine_win32_loop,
	.cleanup = engine_win32_cleanup,
};

#else

struct IOEngine engine_win32 = {
	.name = "win32",
	.init = NULL,
	.add = NULL,
	.remove = NULL,
	.update = NULL,
	.loop = NULL,
	.cleanup = NULL,
};

#endif
