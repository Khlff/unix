CC = gcc
CFLAGS = -Wall -Wextra

all: myinit

myinit: myinit.c
	$(CC) $(CFLAGS) -o myinit $<

clean:
	rm -f myinit /tmp/myinit.log lock_stats.txt config.txt config2.txt config_one.txt
	rm -rf test_dir