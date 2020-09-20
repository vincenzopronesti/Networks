CC = gcc
CFLAGS = -Wall -Werror -Wpedantic -O3

sp: superpeer.c ../util/util.c
	$(CC) $(CFLAGS) -o s superpeer.c ../util/util.c

clean:
	rm -f s 

cleanall:
	rm -f s
