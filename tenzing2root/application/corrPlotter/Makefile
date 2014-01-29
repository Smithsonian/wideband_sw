GRPC = /global/rpcFiles/
all: chunkPlot.h chunkPlot_svc_modified.o corrSaver corrPlotter

chunkPlot.h: $(GRPC)chunkPlot.x Makefile
	cp $(GRPC)chunkPlot.x ./
	rpcgen chunkPlot.x
	gcc -Wall -c -g chunkPlot_xdr.c
	gcc -Wall -c -g chunkPlot_clnt.c
	rm ./chunkPlot.x

chunkPlot_svc_modified.o: chunkPlot_svc_modified.c $(GRPC)chunkPlot.x Makefile
	gcc -Wall -c -g -DDEBUG chunkPlot_svc_modified.c	

corrSaver: corrSaver.c chunkPlot_svc_modified.c corrPlotter.h $(GRPC)chunkPlot.x Makefile
	gcc -Wall -g -o corrSaver \
	-DPG_PPU -DDEBUG -D_POSIX_PTHREAD_SEMANTICS corrSaver.c \
	chunkPlot_svc_modified.o chunkPlot_xdr.o -lnsl -lm

corrPlotter: corrPlotter.o Makefile
	gcc -Wall -g -o corrPlotter -L /usr/X11R6/lib corrPlotter.o \
	$(COMMONLIB)/libdsm.a $(COMMONLIB)/commonLib \
	/application/smapopt/libsmapopt.a \
	-lpthread -lrt -lXm  -lX11 -lm -lnsl


corrPlotter.o: corrPlotter.c corrPlotter.h $(GRPC)chunkPlot.x Makefile
	gcc -Wall -g -c -I/usr/X11R6/include  corrPlotter.c
