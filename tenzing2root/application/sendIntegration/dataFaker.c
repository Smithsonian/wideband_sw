#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "chunkPlot.h"

#define N_ANTENNAS (8)
#define N_SIDEBANDS (2)
#define N_CHUNKS (2)

#define ERROR (-1)
#define OK    ( 0)

float rand_gauss (void) {
  float v1,v2,s;

  do {
    v1 = 2.0 * ((float) rand()/RAND_MAX) - 1;
    v2 = 2.0 * ((float) rand()/RAND_MAX) - 1;

    s = v1*v1 + v2*v2;
  } while ( s >= 1.0 );

  if (s == 0.0)
    return 0.0;
  else
    return (v1*sqrt(-2.0 * log(s) / s));
}

int sendIntegration(int nPoints, double uT, float duration, int chunk,
		    int ant1, int pol1, int ant2, int pol2,
		    float *lsbCross, float *usbCross, int forceTransfer);

int main()
{
  int ant1, ant2, sb, chunk, chan;
  float upper[2*P_N_SWARM_CHANNELS], lower[2*P_N_SWARM_CHANNELS], auto1[P_N_SWARM_CHANNELS], auto2[P_N_SWARM_CHANNELS], amp, phase;
  double uT, dRand;
  time_t timeNow;
  struct tm *gm;

  timeNow = time(NULL);
  gm = gmtime(&timeNow);
  srand((unsigned int)timeNow);
  uT = (float)gm->tm_hour + ((float)gm->tm_min)/60.0 + ((float)gm->tm_sec)/3600.0;
  for (ant1 = 1; ant1 <= N_ANTENNAS; ant1++) {
    for (chan = 0; chan < P_N_SWARM_CHANNELS; chan++) {
      lower[2*chan] = rand_gauss() + sin(2.0*M_PI*(float)(ant1*chan)/((float)(P_N_SWARM_CHANNELS-1)));
    }
    chunk = 0;
    ant2 = ant1;
    printf("Sending autocorr for ant %d\n", ant1);
    sendIntegration(P_N_SWARM_CHANNELS, uT, 30.0, chunk, ant1, 0, ant2, 0, lower, upper, FALSE);
  }
  for (ant1 = 1; ant1 < N_ANTENNAS; ant1++) {
    for (chan = 0; chan < P_N_SWARM_CHANNELS; chan++)
      auto1[chan] = (float)ant1;
    for (ant2 = ant1+1; ant2 <= N_ANTENNAS; ant2++) {
      for (chan = 0; chan < P_N_SWARM_CHANNELS; chan++)
	auto2[chan] = ((float)ant2)*((float)chan)/((float)(P_N_SWARM_CHANNELS-1));
      for (chunk = 0; chunk < N_CHUNKS; chunk++) {
	for (sb = 0; sb < N_SIDEBANDS; sb++) {
	  for (chan = 0; chan < P_N_SWARM_CHANNELS; chan++) {
	    dRand = ((double)rand())/((double)RAND_MAX);
	    if (sb == 0)
	      amp = (10.0 + dRand)*auto1[chan]*auto2[chan];
	    else
	      amp = (10.0 + dRand)*auto1[chan]*auto2[chan]*auto2[chan];
	    if (!((ant1 == 2) && (ant2 == 16)))
	      phase = M_PI*sin(2.0*M_PI*((float)(chunk+1))*((float)(chan*ant1*ant2))/((float)(P_N_SWARM_CHANNELS-1)));
	    else
	      phase = 0.0;
	    if (sb == 0) {
	      upper[2*chan    ] = amp*cos(phase);
	      upper[2*chan + 1] = amp*sin(phase);
	    } else {
	      lower[2*chan    ] = amp*cos(phase);
	      lower[2*chan + 1] = amp*sin(phase);
	    }
	  }
	}
	sendIntegration(P_N_SWARM_CHANNELS, uT, 30.0, chunk, ant1, 0, ant2, 0, lower, upper, FALSE);
      }
    }
  }
  return OK;
}
