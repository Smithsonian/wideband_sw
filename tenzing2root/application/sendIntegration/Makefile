RPC=/global/rpcFiles/
dataFaker: dataFaker.c sendIntegration.c ./Makefile chunkPlot.h chunkPlot_clnt.o chunkPlot_xdr.o dataCatcher.h dataCatcher_clnt.o dataCatcher_xdr.o
	gcc -Wall -o dataFaker -lm dataFaker.c sendIntegration.c chunkPlot_clnt.o \
	chunkPlot_xdr.o dataCatcher_clnt.o dataCatcher_xdr.o

chunkPlot.h: $(RPC)chunkPlot.x ./Makefile
	cp $(RPC)chunkPlot.x ./
	rpcgen chunkPlot.x

chunkPlot_clnt.o: chunkPlot_clnt.c ./Makefile
	gcc -c chunkPlot_clnt.c

chunkPlot_clnt.c: $(RPC)chunkPlot.x ./Makefile
	cp $(RPC)chunkPlot.x ./
	rpcgen chunkPlot.x

chunkPlot_xdr.o: chunkPlot_xdr.c ./Makefile
	gcc -c chunkPlot_xdr.c

chunkPlot_xdr.c: $(RPC)chunkPlot.x ./Makefile
	cp $(RPC)chunkPlot.x ./
	rpcgen chunkPlot.x

dataCatcher.h: $(RPC)dataCatcher.x ./Makefile
	cp $(RPC)dataCatcher.x ./
	rpcgen dataCatcher.x

dataCatcher_clnt.o: dataCatcher_clnt.c ./Makefile
	gcc -c dataCatcher_clnt.c

dataCatcher_clnt.c: $(RPC)dataCatcher.x ./Makefile
	cp $(RPC)dataCatcher.x ./
	rpcgen dataCatcher.x

dataCatcher_xdr.o: dataCatcher_xdr.c ./Makefile
	gcc -c dataCatcher_xdr.c

dataCatcher_xdr.c: $(RPC)dataCatcher.x ./Makefile
	cp $(RPC)dataCatcher.x ./
	rpcgen dataCatcher.x

