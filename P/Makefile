CC = gcc
CFLAGS = -Wall -Werror -Wpedantic -O3

p: peer.c ../util/util.c
	$(CC) $(CFLAGS) -o p peer.c ../util/util.c

clean:
	rm -f p 

cleanall:
	rm -f p
