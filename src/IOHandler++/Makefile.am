##Process this file with automake to create Makefile.in
ACLOCAL_AMFLAGS = -I m4

noinst_LTLIBRARIES = libiohandler.cpp.la

libiohandler_cpp_la_LIBADD = ../IOHandler/libiohandler.la
libiohandler_cpp_la_SOURCES = IOHandler.cpp \
IOSocket.cpp

all-local: libiohandler.cpp.la
	