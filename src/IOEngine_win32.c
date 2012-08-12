/* IOEngine_win32.c - IOMultiplexer
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
#include "IOEngine.h"

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

static struct IODescriptor *engine_win32_get_iofd(int fd) {
    struct IODescriptor *iofd;
    for(iofd = first_descriptor; iofd; iofd = iofd->next) {
        if(iofd->fd == fd)
            return iofd;
    }
    return NULL;
}

static LRESULT CALLBACK engine_win32_wndproc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    struct IODescriptor *iofd;
    int events;
    struct timeval now, tdiff;
    
    gettimeofday(&now, NULL);

    if (hWnd == ioset_window) switch (uMsg)
    {
    case IDT_TIMER1:
        return 0;
    case IDT_TIMER2:
        //User Timer
        while(timer_priority) {
            tdiff.tv_sec = timer_priority->timeout.tv_sec - now.tv_sec;
            tdiff.tv_usec = timer_priority->timeout.tv_usec - now.tv_usec;
            if(tdiff.tv_sec < 0 || (tdiff.tv_sec == 0 && tdiff.tv_usec <= 0)) {
                iohandler_events(timer_priority, 0, 0);
                iohandler_close(timer_priority); //also sets timer_priority to the next timed element
                continue;
            }
            break;
        }
        return 0;
    case IDT_SOCKET:
        iofd = engine_win32_get_iofd(wParam);
        events = WSAGETSELECTEVENT(lParam);
        
        iohandler_events(iofd, (events & (FD_READ | FD_ACCEPT | FD_CLOSE)) != 0, (events & (FD_WRITE | FD_CONNECT)) != 0);
        return 0;
    case WM_QUIT:
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static int engine_win32_init() {
    WNDCLASSEX wcx;
    HINSTANCE hinst;
    WSADATA wsadata;
    
    // Start Windows Sockets.
    if (WSAStartup(MAKEWORD(2, 0), &wsadata)) {
        iohandler_log(IOLOG_FATAL, "Unable to start Windows Sockets");
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

static long engine_win32_events(struct IODescriptor *iofd) {
    switch (iofd->state) {
    case IO_CLOSED:
        return 0;
    case IO_LISTENING:
        return FD_ACCEPT;
    case IO_CONNECTING:
        return FD_CONNECT;
    case IO_CONNECTED:
    case IO_SSLWAIT:
        return FD_READ | FD_CLOSE | (iohandler_wants_writes(iofd) ? FD_WRITE : 0);
    }
    return 0;
}

static void engine_win32_update(struct IODescriptor *iofd) {
    long events;
    
    if(iofd->type == IOTYPE_STDIN)
        return;
    
    events = engine_win32_events(iofd);
    WSAAsyncSelect(iofd->fd, ioset_window, IDT_SOCKET, events);
}

static void engine_win32_add(struct IODescriptor *iofd) {
    if(iofd->type == IOTYPE_STDIN)
        return;
    
    engine_win32_update(iofd);
}

static void engine_win32_remove(struct IODescriptor *iofd) {
    unsigned long ulong;
    
    if(iofd->type == IOTYPE_STDIN)
        return;
    
    WSAAsyncSelect(iofd->fd, ioset_window, IDT_SOCKET, 0);
    
    ulong = 0;
    ioctlsocket(iofd->fd, FIONBIO, &ulong);
}

static void engine_win32_loop(struct timeval *timeout) {
    MSG msg;
    BOOL not_really_bool;
    int msec, cmsec, sett2;
    struct timeval now, tdiff;
    struct IODescriptor *iofd, *tmp_iofd;
    
    gettimeofday(&now, NULL);
    
    for(iofd = first_descriptor; iofd; iofd = tmp_iofd) {
        tmp_iofd = iofd->next;
        if(iofd->type == IOTYPE_STDIN) {
            #ifdef WIN32
            //WIN32 doesn't support stdin within select
            //just try to read the single events from the console
            DWORD dwRead;
            INPUT_RECORD inRecords[128];
            unsigned int i;
            int read_bytes = 0;
            GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &dwRead);
            if(dwRead)
                ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &inRecords[0], 128, &dwRead);
            for (i = 0; i < dwRead; ++i) {
                if (inRecords[i].EventType == KEY_EVENT) {
                    const char c = inRecords[i].Event.KeyEvent.uChar.AsciiChar;
                    if (inRecords[i].Event.KeyEvent.bKeyDown && c != 0) {
                        iofd->readbuf.buffer[iofd->readbuf.bufpos + read_bytes] = c;
                        read_bytes++;
                    }
                }
            }
            if(read_bytes)
                iohandler_events(iofd, read_bytes, 0);
            if(read_bytes >= 128) {
                timeout->tv_sec = 0;
                timeout->tv_usec = 1;
                //minimal timeout
            } else {
                timeout->tv_sec = 0;
                timeout->tv_usec = 100000;
            }
            #else
            if(iofd->fd > fds_size)
                fds_size = iofd->fd;
            FD_SET(iofd->fd, &read_fds);
            #endif
        }
    }
    
    // Make sure we are woken up after the appropriate time.
    msec = (timeout->tv_sec * 1000) + (timeout->tv_usec / 1000);
    SetTimer(ioset_window, IDT_TIMER1, msec, NULL);
    
    //set additional User Timer (if ther's one)
    sett2 = 0;
    while(timer_priority) {
        tdiff.tv_sec = timer_priority->timeout.tv_sec - now.tv_sec;
        tdiff.tv_usec = timer_priority->timeout.tv_usec - now.tv_usec;
        if(tdiff.tv_sec < 0 || (tdiff.tv_sec == 0 && tdiff.tv_usec < 1000)) {
            iohandler_events(timer_priority, 0, 0);
            iohandler_close(timer_priority); //also sets timer_priority to the next timed element
            continue;
        } else if(tdiff.tv_usec < 0) {
            tdiff.tv_sec--;
            tdiff.tv_usec += 1000000; //1 sec
        }
        cmsec = (tdiff.tv_sec * 1000) + (tdiff.tv_usec / 1000);
        if(cmsec < msec) {
            sett2 = 1;
            msec = cmsec;
        }
        break;
    }
    if(sett2)
        SetTimer(ioset_window, IDT_TIMER2, msec, NULL);
    
    // Do a blocking read of the message queue.
    not_really_bool = GetMessage(&msg, NULL, 0, 0);
    KillTimer(ioset_window, IDT_TIMER1);
    if(sett2)
        KillTimer(ioset_window, IDT_TIMER2);
    if (not_really_bool <=0)
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
