CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -g

.PHONY: all default clean

default: all

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.o -lpthread

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c -o aesdsocket.o

clean:
	rm -f aesdsocket aesdsocket.o
