CC = gcc

all:
	+$(MAKE) -C BS
	+$(MAKE) -C SP
	+$(MAKE) -C P

clean:
	+$(MAKE) -C BS clean
	+$(MAKE) -C SP clean
	+$(MAKE) -C P clean	
