CC := gcc
CFLAGS += -Wall -Wextra -std=gnu99 -O2
LIBS := -lm

all: subsync

subsync: subsync.c list.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LIBS) -o $@

clean:
	@rm -f subsync
