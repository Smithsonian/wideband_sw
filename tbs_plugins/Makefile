IDIR = ../../katcp_devel

all: example alarm

example: example.o
	$(CROSS_COMPILE)gcc -o example.so example.o -shared

example.o: example.c
	$(CROSS_COMPILE)gcc -fPIC -Wall -o example.o -g -c example.c -I$(IDIR)/katcp -I$(IDIR)/tcpborphserver3

alarm: alarm.o
	$(CROSS_COMPILE)gcc -o alarm.so alarm.o -shared

alarm.o: alarm.c
	$(CROSS_COMPILE)gcc -fPIC -Wall -o alarm.o -g -c alarm.c -I$(IDIR)/katcp -I$(IDIR)/tcpborphserver3

clean:
	rm *.o *.so
