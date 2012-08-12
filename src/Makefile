
CC      = gcc
CFLAGS  = -g -O0 -Wall -Wshadow -Werror
LDFLAGS = -lws2_32

OBJ     = IOEngine_epoll.o IOEngine_kevent.o IOEngine_select.o IOEngine_win32.o IOHandler.o IOHandler_SSL.o main.o

all: $(OBJ)
	$(CC) $(CFLAGS) -oiotest $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<
