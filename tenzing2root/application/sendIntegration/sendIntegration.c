/*
  sendIntegration.c
  Raoul Taco Machilvich
  First version, July 1, 2013

    This function accepts visibility data from the SWARM correlator, and
transmits it to the process corrSaver (for display) which sends to dataCatcher
(for storage in the science data set).   The transfers are done via RPC client
calls.   By default this function will not transmit data for baselines
which include an antenna which is not in the array.   The forceTransfer
parameter allows the caller to force the data to be transmitted whether or not
the antennas are in the project.

 */
#include <stdio.h>
#include <time.h>
#include <rpc/rpc.h>
#include "chunkPlot.h"

#define N_ANTENNAS (8)
#define N_SIDEBANDS (2)
#define N_CHUNKS (6)

#define ERROR (-1)
#define OK    ( 0)

int getAntennaList(int *list);

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
		    float *lsbCross, float *usbCross, int forceTransfer)
{
  int i, antennaInArray[11];
  static CLIENT *corrSaverCl = NULL;

  if ((antennaInArray[ant1] && antennaInArray[ant2]) || forceTransfer) {
    if (chunk >= N_CHUNKS) {
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
	sWARMData.lSB[i] = lsbCross[2*i];
    else {
      for (i = 0; i < 2*nPoints; i++) {
	sWARMData.lSB[i] = lsbCross[i];
	sWARMData.uSB[i] = usbCross[i];
      }
    }

    /* Send the data to obscon only */
    if (corrSaverCl == NULL) {
      if (!(corrSaverCl = clnt_create("obscon", CHUNKPLOTPROG, CHUNKPLOTVERS, "tcp"))) {
	fprintf(stderr, clnt_spcreateerror("obscon"));
	return(ERROR);
      }
    }
    if (printResults(plot_swarm_data_1(&sWARMData, corrSaverCl))) {
      fprintf(stderr, "Error returned from corrPlotter call\n");
      clnt_destroy(corrSaverCl);
      corrSaverCl = NULL;
    }
  }
  return OK;
}
