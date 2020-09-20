CC = gcc
CFLAGS = -Wall -Werror -Wpedantic -O3

b: bootstrap.c ../util/util.c
	$(CC) $(CFLAGS) -o b bootstrap.c ../util/util.c

clean:
	rm -f b 

cleanall:
	rm -f b
