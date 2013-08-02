#include <stdio.h>
#include <time.h>
#include <rpc/rpc.h>
#include "chunkPlot.h"
#include "dataCatcher.h"

#define N_ANTENNAS (8)
#define N_SIDEBANDS (2)
#define N_CHUNKS (2)

#define ERROR (-1)
#define OK    ( 0)

int printResults(statusStructure *results)
{
  if (results == NULL) {
    fprintf(stderr, "RPC call returned a NULL pointer\n");
    return(ERROR);
  } else if (results->rt_code) {
    printf("results->rt_code = %d\n", results->rt_code);
    return(ERROR);
  }
  return(OK);
}

pSWARMUVBlock sWARMData;

int sendIntegration(int nPoints, double uT, float duration, int chunk,
		    int ant1, int pol1, int ant2, int pol2,
		    float *lsbCross, float *usbCross)
{
  int i;
  CLIENT *cl;

  if ((chunk != 0) && (chunk != 1)) {
    fprintf(stderr, "sendIntegration called with illegal chunk number (%d) - aborting\n", chunk);
    return(ERROR);
  }
  sWARMData.nChannels = nPoints;
  sWARMData.uT = uT;
  sWARMData.duration = duration;
  sWARMData.ant1 = ant1;
  sWARMData.pol1 = pol1;
  sWARMData.ant2 = ant2;
  sWARMData.pol2 = pol2;
  sWARMData.chunk = chunk;
  if (ant1 == ant2)
    for (i = 0; i < nPoints; i++)
      sWARMData.lSB[i] = lsbCross[i];
  else {
    for (i = 0; i < 2*nPoints; i++) {
      sWARMData.lSB[i] = lsbCross[i];
      sWARMData.uSB[i] = usbCross[i];
    }
  }

  /* Send the data to corrPlotter */
  if (!(cl = clnt_create("obscon", CHUNKPLOTPROG, CHUNKPLOTVERS, "tcp"))) {
    clnt_pcreateerror("obscon");
    return(ERROR);
  }
  if (printResults(plot_swarm_data_1(&sWARMData, cl)))
    fprintf(stderr, "Error returned from corrPlotter call\n");
  clnt_destroy(cl);

  /* Send the data to dataCatcher */
  if (!(cl = clnt_create("hcn", DATACATCHERPROG, DATACATCHERVERS, "tcp"))) {
    clnt_pcreateerror("hcn");
    return(ERROR);
  }
  if (printResults((statusStructure *)catch_swarm_data_1((dSWARMUVBlock *)(&sWARMData), cl)))
    fprintf(stderr, "Error returned from dataCatcher call\n");
  clnt_destroy(cl);

  return OK;
}
