CC = gcc
CFLAGS = -Wall -Wextra

all: locker

locker: locker.c
	$(CC) $(CFLAGS) -o locker $<

clean:
	rm -f locker test_file.lck lock_stats.txt