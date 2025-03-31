CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -g
LDFLAGS = -lrt -pthread

BINARIES = master view player

all: $(BINARIES)

master: master.c
	$(CC) $(CFLAGS) -o master master.c $(LDFLAGS)

view: view.c
	$(CC) $(CFLAGS) -o view view.c $(LDFLAGS)

player: player.c
	$(CC) $(CFLAGS) -o player player.c $(LDFLAGS)

clean:
	rm -f $(BINARIES)

.PHONY: all clean
