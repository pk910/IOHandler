##Process this file with automake to create Makefile.in
ACLOCAL_AMFLAGS = -I m4

libiohandler_a_SOURCES = compat/utime.c \
    compat/inet.c \
    IOHandler.c \
    IODNSEngine_cares.c \
    IODNSEngine_default.c \
    IODNSLookup.c \
    IOEngine_epoll.c \
    IOEngine_kevent.c \
    IOEngine_select.c \
    IOEngine_win32.c \
    IOGarbageCollector.c \
    IOLog.c \
    IOSockets.c \
    IOTimer.c

noinst_LIBRARIES = libiohandler.a
