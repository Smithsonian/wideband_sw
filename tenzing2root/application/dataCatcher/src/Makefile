INC = ../include
GLOBALRPC = /global/rpcFiles
GLOBALINC = /global/include/
STORAGEBIN = /application/bin/
TEST = ../test/
COMMONLIB = /common/lib/
COMMON = /common/
COMMONINC = /common/include/
CFLAGS = -Wall -O3 -g -D_FILE_OFFSET_BITS=64
IS_DOUBLE_BANDWIDTH = /global/isDoubleBandwidth/isDoubleBandwidth.c
IS_FULL_POLARIZATION = /global/isFullPolarization/isFullPolarization.c

all: $(INC)/dataCatcher.h $(INC)/statusServer.h $(INC)/setLO.h \
        dataCatcher_svc_modified.o dataCatcher_xdr.o novas.o \
        novascon.o statusServer_clnt.o statusServer_xdr.o setLO_clnt.o setLO_xdr.o \
	$(TEST)/dataCatcher

install: all
	cp $(TEST)/dataCatcher $(STORAGEBIN)/

clean:
	- rm *.o *.x $(TEST)/dataCatcher

$(INC)/dataCatcher.h: $(GLOBALRPC)/dataCatcher.x ./Makefile
	cp $(GLOBALRPC)/dataCatcher.x ./
	rpcgen ./dataCatcher.x
	mv dataCatcher.h $(INC)/
	rm dataCatcher.x dataCatcher_svc.c dataCatcher_clnt.c \
	   dataCatcher_xdr.c

dataCatcher_svc_modified.o: $(GLOBALRPC)/dataCatcher.x dataCatcher_svc_modified.c ./Makefile
	gcc $(CFLAGS) -c -I$(INC) dataCatcher_svc_modified.c

dataCatcher_xdr.o: $(GLOBALRPC)/dataCatcher.x ./Makefile
	cp $(GLOBALRPC)/dataCatcher.x ./
	rpcgen ./dataCatcher.x
	gcc $(CFLAGS) -c dataCatcher_xdr.c
	rm dataCatcher.x dataCatcher_svc.c dataCatcher_clnt.c \
	   dataCatcher_xdr.c dataCatcher.h

$(INC)/statusServer.h: $(GLOBALRPC)/statusServer.x ./Makefile
	cp $(GLOBALRPC)/statusServer.x ./
	rpcgen ./statusServer.x
	mv statusServer.h $(INC)/
	rm statusServer.x statusServer_svc.c statusServer_clnt.c \
	   statusServer_xdr.c

$(INC)/setLO.h: $(GLOBALRPC)/setLO.x ./Makefile
	cp $(GLOBALRPC)/setLO.x ./
	rpcgen ./setLO.x
	mv setLO.h $(INC)/
	rm setLO.x setLO_svc.c setLO_clnt.c \
	   setLO_xdr.c

statusServer_clnt.o: $(GLOBALRPC)/statusServer.x ./Makefile
	cp $(GLOBALRPC)/statusServer.x ./
	rpcgen ./statusServer.x
	gcc $(CFLAGS) -c statusServer_clnt.c
	rm statusServer.x statusServer_svc.c statusServer_clnt.c \
	   statusServer_xdr.c statusServer.h

statusServer_xdr.o: $(GLOBALRPC)/statusServer.x ./Makefile
	cp $(GLOBALRPC)/statusServer.x ./
	rpcgen ./statusServer.x
	gcc $(CFLAGS) -c statusServer_xdr.c
	rm statusServer.x statusServer_svc.c statusServer_clnt.c \
	   statusServer_xdr.c statusServer.h

setLO_clnt.o: $(GLOBALRPC)/setLO.x ./Makefile
	cp $(GLOBALRPC)/setLO.x ./
	rpcgen ./setLO.x
	gcc $(CFLAGS) -c setLO_clnt.c
	rm setLO.x setLO_svc.c setLO_clnt.c \
	   setLO_xdr.c setLO.h

setLO_xdr.o: $(GLOBALRPC)/setLO.x ./Makefile
	cp $(GLOBALRPC)/setLO.x ./
	rpcgen ./setLO.x
	gcc $(CFLAGS) -c setLO_xdr.c
	rm setLO.x setLO_svc.c setLO_clnt.c \
	   setLO_xdr.c setLO.h

novas.o: ./novas.c $(INC)/novas.h $(INC)/novascon.h ./Makefile
	gcc $(CFLAGS) -c -I$(INC) novas.c

novascon.o: ./novascon.c $(INC)/novas.h $(INC)/novascon.h ./Makefile
	gcc $(CFLAGS) -c -I$(INC) novascon.c

$(TEST)/dataCatcher: $(INC)/dataCatcher.h dataCatcher.c \
        $(INC)/mirStructures.h $(INC)/statusServer.h $(INC)/setLO.h \
	dataCatcher_svc_modified.c $(COMMON)/lib/commonLib ./Makefile $(IS_DOUBLE_BANDWIDTH) \
	$(IS_FULL_POLARIZATION)
	gcc $(CFLAGS) -o $(TEST)/dataCatcher -I$(INC) -I$(COMMONINC) \
	-I$(GLOBALINC) dataCatcher.c $(IS_DOUBLE_BANDWIDTH) \
	$(IS_FULL_POLARIZATION) dataCatcher_svc_modified.o dataCatcher_xdr.o \
	novas.o novascon.o statusServer_clnt.o statusServer_xdr.o setLO_clnt.o setLO_xdr.o \
	-lpthread -lrt \
	$(COMMON)/lib/commonLib \
	-lm -lnsl
