IDIR = ../../katcp_devel

example: example.o
	cc -o example.so example.o -shared

example.o: example.c
	cc -fPIC -Wall -o example.o -g -c example.c -I$(IDIR)/katcp -I$(IDIR)/tcpborphserver3

clean:
	rm *.o *.so
