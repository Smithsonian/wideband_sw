#include <math.h>
#include <rpc/rpc.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h> 
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "corrPlotter.h"
#include "chunkPlot.h"

int debugMessagesOn = FALSE;
int doubleBandwidth;

statusStructure *result1;
statusStructure *start_sma1_1(token, cl)
int *token;
CLIENT *cl;
{
  static int firstCall = TRUE;

  if (firstCall) {
    result1 = (statusStructure *)malloc(sizeof(statusStructure));
    if (result1 == NULL)
      perror("start_sma1 malloc");
    firstCall = FALSE;
  }
  result1->rt_code = 0;
  if (debugMessagesOn)
    printf("I'm in start_sma1 - I have no idea why.   Token = %d\n", token);
  return((statusStructure *)result1);
}

void print_sm_structure(correlatorDef *ptr)
{
  int crate;
  
  printf("Correlator summary (updating = %d)\n", ptr->updating);
  printf("dataHeader:\n");
  for (crate = 0; crate < N_CRATES; crate++) {
    int bsln, band;
    
    printf("crate%d: crateActive: %d, scanNumber: %d, UTCTime: %f, intTime: %f\n",
	   crate, ptr->header.crateActive[crate],
	   ptr->header.scanNumber[crate],
	   ptr->header.UTCTime[crate],
	   ptr->header.intTime[crate]);
    if (ptr->header.crateActive[crate]) {
      printf("\t Chunk1: %d Chunk2: %d Chunk3: %d Chunk4: %d\n",
	     ptr->crate[crate].description.pointsPerChunk[0][0],
	     ptr->crate[crate].description.pointsPerChunk[0][1],
	     ptr->crate[crate].description.pointsPerChunk[0][2],
	     ptr->crate[crate].description.pointsPerChunk[0][3]);
      printf("BSLNS:\n");
      for (band = 0; band < N_IFS; band++) {
	printf("Band %d: ", band);
	for (bsln = 0; bsln < N_BASELINES_PER_CRATE; bsln++)
	  printf("%d ", ptr->crate[crate].description.baselineInUse[band][bsln]);
	printf("\n");
      }
    }
  }
}

statusStructure *wResult1;
statusStructure *start_sma1_1_svc(token, dummy)
int *token;
struct svc_req *dummy;
{
  CLIENT *cl;

  wResult1 = start_sma1_1(token, cl);
}

/* Print the entire contents (except for raw data) of a visibility bundle    */
void print_vis_bundle(pCrateUVBlock *bundle)
{
  int set;

  printf("INCOMING BUNDLE:\t Source = \"%s\"\tCrate Number = %d\tblock = %d\tscan type %d\n",
         bundle->sourceName, bundle->crateNumber, bundle->blockNumber, bundle->scanType);
  printf("Scan number: %d\n", bundle->scanNumber);
  printf("Number of visibility sets: %d\n", bundle->set.set_len);
  for (set = 0; set < bundle->set.set_len; set++) {
    printf("Set %d, nPoints %d, Ant1: %d, Ant2: %d\nBand1: %d Band2: %d, Chunk[1]: %d Chunk[2]: %d\n",
           set,
           bundle->set.set_val[set].nPoints,
           bundle->set.set_val[set].antennaNumber[1], bundle->set.set_val[set].antennaNumber[2],
           bundle->set.set_val[set].rxBoardHalf, bundle->set.set_val[set].rxBoardHalf, 
           bundle->set.set_val[set].chunkNumber, bundle->set.set_val[set].chunkNumber);
    printf("bundle->set.set_val[%d].lags.channel.channel_len: %d\n",
           set, bundle->set.set_val[set].lags.channel.channel_len);
    printf("bundle->set.set_val[%d].lags.channel.channel_val: 0x%x\n",
           set, (unsigned int)bundle->set.set_val[set].lags.channel.channel_val);
    if ((bundle->set.set_val[set].lags.channel.channel_len > 0) &&
        (bundle->set.set_val[set].lags.channel.channel_val != NULL)) {
      printf("%d lags at 0x%x, first = %e, last = %e\n",
             bundle->set.set_val[set].lags.channel.channel_len,
             (unsigned int)bundle->set.set_val[set].lags.channel.channel_val,
             bundle->set.set_val[set].lags.channel.channel_val[0],
             bundle->set.set_val[set].lags.channel.channel_val[bundle->set.set_val[set].lags.channel.channel_len-1]);
    }

    printf("bundle->set.set_val[%d].real.real_len: %d\n",
           set, bundle->set.set_val[set].real.real_len);
    printf("bundle->set.set_val[%d].real.real_val: 0x%x\n",
           set, (unsigned int)bundle->set.set_val[set].real.real_val);
    if ((bundle->set.set_val[set].real.real_len != 0) ||
        (bundle->set.set_val[set].real.real_val != NULL)) {
      printf(".real_len = %d, .real_val = 0x%x\n",
             bundle->set.set_val[set].real.real_len,
             (unsigned int)bundle->set.set_val[set].real.real_val);
      printf(".imag_len = %d, .imag_val = 0x%x\n",
             bundle->set.set_val[set].imag.imag_len,
             (unsigned int)bundle->set.set_val[set].imag.imag_val);
      printf(".real_val[0]->channel.channel_len = %d\t.real_val[0]->channel.channel_val = 0x%x\n",
             bundle->set.set_val[set].real.real_val[0].channel.channel_len,
             (unsigned int)bundle->set.set_val[set].real.real_val[0].channel.channel_val);
      if (bundle->set.set_val[set].real.real_len == 2) {
        printf(".real_val[1]->channel.channel_len = %d\t.real_val[1]->channel.channel_val = 0x%x first %f\n",
             bundle->set.set_val[set].real.real_val[1].channel.channel_len,
             (unsigned int)bundle->set.set_val[set].real.real_val[1].channel.channel_val,
	       bundle->set.set_val[set].real.real_val[1].channel.channel_val[10]);
      }
    }

    printf("bundle->set.set_val[%d].imag.imag_len: %d\n",
           set, bundle->set.set_val[set].imag.imag_len);
    printf("bundle->set.set_val[%d].imag.imag_val: 0x%x\n",
           set, (unsigned int)bundle->set.set_val[set].imag.imag_val);
    if ((bundle->set.set_val[set].imag.imag_len != 0) ||
        (bundle->set.set_val[set].imag.imag_val != NULL)) {
      printf(".imag_len = %d, .imag_val = 0x%x\n",
             bundle->set.set_val[set].imag.imag_len,
             (unsigned int)bundle->set.set_val[set].imag.imag_val);
      printf(".imag_len = %d, .imag_val = 0x%x\n",
             bundle->set.set_val[set].imag.imag_len,
             (unsigned int)bundle->set.set_val[set].imag.imag_val);
    }
  }
}

correlatorDef *cptr = NULL;

void makeSharedMemory(void)
{
  int returnCode;
  FILE *shmmax;
  
  if (debugMessagesOn)
    printf("And it's the first call\n");
  shmmax = fopen("/proc/sys/kernel/shmmax", "w");
  if (shmmax == NULL) {
    perror("fopen on /proc/sys/kernel/shmmax");
  } else {
    fprintf(shmmax, "%d", sizeof(correlatorDef)+1);
    fclose(shmmax);
  }
  if (debugMessagesOn || 1)
    printf("The size of the correlator structure is %d bytes\n",
	   sizeof(correlatorDef));
  printf("PLT_KEY_ID = %d\n", PLT_KEY_ID);
  returnCode = shmget(PLT_KEY_ID,
		      sizeof(correlatorDef),
		      IPC_CREAT | 0666);
  if (returnCode < 0) {
    perror("creating main shared memory structure");
    exit(-1);
  }
  if (debugMessagesOn)
    printf("Create successful\n");
  if (debugMessagesOn)
    printf("cptr before shmat call = 0x%x\n", cptr);
  cptr = shmat(returnCode, (char *)0, 0);
  if ((int)cptr < 0)
    perror("shmat call");
  if (debugMessagesOn)
    printf("cptr after shmat call = 0x%x\n", cptr);
  if (debugMessagesOn)
    printf("End of firstCall activities\n");
  bzero(cptr, sizeof(*cptr));
}

statusStructure *result2;
statusStructure *plot_visibilities_1(data, cl)
pCrateUVBlock *data;
CLIENT *cl;
{
  static int firstCall = TRUE;
  int found;
  int crate, i, band, set, bsln[N_IFS], bslnTable[N_IFS][N_BASELINES_PER_CRATE][N_ANTENNAS_PER_BASELINE];
  pVisibilitySet *dataPointers[N_IFS][N_BASELINES_PER_CRATE][N_CHUNKS];
  FILE *crateFile;

  if (debugMessagesOn)
    printf("plot_visibilities_1 called\n");
  if (firstCall) {
    if (cptr == NULL)
      makeSharedMemory();
    result2 = (statusStructure *)malloc(sizeof(*cptr));
    if (result2 == NULL)
      perror("send_visibilities malloc");
    firstCall = FALSE;
  }
  cptr->updating = TRUE;
  if (debugMessagesOn && 0)
    print_vis_bundle(data);
  for (i = 0; i < N_CRATES; i++)
    cptr->header.crateActive[i] = FALSE;
  crateFile = fopen("/global/projects/cratesInArray", "r");
  if (crateFile != NULL) {
    int activeCrates[13], crateCount;

    crateCount = fscanf(crateFile,
			"%d %d %d %d %d %d %d %d %d %d %d %d",
			&activeCrates[1], &activeCrates[2], &activeCrates[3],
			&activeCrates[4], &activeCrates[5], &activeCrates[6],
			&activeCrates[7], &activeCrates[8], &activeCrates[9],
			&activeCrates[10], &activeCrates[11], &activeCrates[12]);

    for (i = 1; i <= crateCount; i++) {
      /*
      printf("**** Setting crate %d to active\n", activeCrates[i]-1);
      */
      cptr->header.crateActive[activeCrates[i]-1] = TRUE;
    }
    fclose(crateFile);
  } else
    perror("Error opening cratesInArray file - all crates will be marked inactive");
  for (i = 0; i < N_IFS; i++)
    cptr->header.receiverActive[i] = FALSE;
  crateFile = fopen("/global/configFiles/doubleBandwidth", "r");
  if (crateFile == NULL) {
    int activeReceivers[3], receiverCount;
      
    doubleBandwidth = FALSE;
    crateFile = fopen("/global/projects/receiversInArray", "r");
    if (crateFile != NULL) {
      receiverCount = fscanf(crateFile,
			     "%d %d",
			     &activeReceivers[1], &activeReceivers[2]);
      for (i = 1; i <= receiverCount; i++)
	cptr->header.receiverActive[activeReceivers[i]-1] = TRUE;
      fclose(crateFile);
    } else
      perror("Error opening receiversInArray file - all receivers will be marked inactive");
    
  }  else {
    cptr->header.receiverActive[0] = cptr->header.receiverActive[1] = TRUE;
    doubleBandwidth = TRUE;
    fclose(crateFile);
  }
  if (debugMessagesOn)
    printf("Message from crate %d, processing block %d\n",
	   data->crateNumber, data->blockNumber);
  crate = data->crateNumber;
  result2->rt_code = 0;
  /*
    Update general header variables
  */
  cptr->header.scanNumber[crate-1] = data->scanNumber;
  cptr->header.intTime[crate-1] = data->intTime;
  cptr->header.UTCTime[crate-1] = data->UTCtime;
  /*
    Update parameters about baselines
  */
  for (band = 0; band < N_IFS; band++) {
    for (bsln[band] = 0; bsln[band] < N_BASELINES_PER_CRATE; bsln[band]++) {
      cptr->crate[crate-1].description.baselineInUse[band][bsln[band]] = FALSE;
      bslnTable[band][bsln[band]][0] = bslnTable[band][bsln[band]][1] = 0;
    }
  }
  for (band = 0; band < N_IFS; band++) {
    bsln[band] = 0;
    for (set = 0; set < 4; set++)
      cptr->crate[crate-1].description.pointsPerChunk[band][set] = 0;
  }
  for (set = 0; set < data->set.set_len; set++) {
    int bandNum;
    pVisibilitySet *sptr;

    band = data->set.set_val[set].rxBoardHalf;
    if (band == 0)
      bandNum = 1;
    else
      bandNum = 0;
    sptr = &(data->set.set_val[set]);
    cptr->crate[crate-1].description.pointsPerChunk[band][sptr->chunkNumber-1] =
      sptr->nPoints;
    if (debugMessagesOn)
      printf("For set %d: crate-1 = %d\tband = %d\tchunkNumber-1 = %d\tnPoints = %d\n",
	     set, crate-1, band, sptr->chunkNumber-1, sptr->nPoints);
    i = 0;
    found = FALSE;
    while ((i < bsln[band]) && (!found)) {
      if ((bslnTable[band][i][0] == sptr->antennaNumber[1]) &&
	  (bslnTable[band][i][1] == sptr->antennaNumber[2]))
	found = TRUE;
      else
	i++;
    }
    if (!found) {
      cptr->crate[crate-1].description.baselineInUse[band][bsln[band]] = TRUE;
      cptr->crate[crate-1].data[bsln[band]].antenna[0] = bslnTable[band][i][0] =
	sptr->antennaNumber[1];
      cptr->crate[crate-1].data[bsln[band]].antenna[1] = bslnTable[band][i][1] =
	sptr->antennaNumber[2];
      bsln[band]++;
    }
    /*
    printf("band: %d active: %d\n", band, cptr->header.receiverActive[bandNum]);
    */
    if (cptr->header.receiverActive[bandNum]) {
      for (i = 0; i < N_SAMPLER_LEVELS; i++) {
	cptr->crate[crate-1].data[bsln[band]-1].counts[band][0][(sptr->chunkNumber)-1][i] =
	  sptr->counts[i];
	if (debugMessagesOn)
	  printf("1: Band: %d crate: %d, bsln-1 %d, (%d-%d), chunk %d, level %d = %f\n", bandNum,
		 crate, bsln[band]-1, sptr->antennaNumber[1], sptr->antennaNumber[2],
		 sptr->chunkNumber, i,
		 sptr->counts[i]);
      }
      for (i = 0; i < N_SAMPLER_LEVELS; i++) {
	cptr->crate[crate-1].data[bsln[band]-1].counts[band][1][(sptr->chunkNumber)-1][i] =
	  sptr->counts[i+N_SAMPLER_LEVELS];
 	if (debugMessagesOn)
	  printf("2: Band: %d crate: %d, bsln-1 %d, ant: %d, chunk %d, level %d = %f\n", bandNum,
		 crate, bsln[band]-1, sptr->antennaNumber[2], sptr->chunkNumber, i,
		 sptr->counts[i+N_SAMPLER_LEVELS]);
      }
    }
    dataPointers[band][bsln[band]-1][(sptr->chunkNumber)-1] = sptr;
  }
  if (debugMessagesOn)
    printf("Start copying into shared memory\n");
  /*
    Now that we know all about the data, copy it into the shared memory
  */
  for (band = 0; band < N_IFS; band++) {
    bsln[band] = 0;
    while ((cptr->crate[crate-1].description.baselineInUse[band][bsln[band]]) &&
	   (bsln[band] < N_BASELINES_PER_CRATE)) {
      int chunkOffset[N_IFS], chunk;
      
      chunkOffset[0] = chunkOffset[1] = 0;
      for (chunk = 0; chunk < N_CHUNKS; chunk++) {
	int channel;
	int bandNum;
	
	if (band == 0)
	  bandNum = 1;
	else
	  bandNum = 0;
	if (cptr->header.receiverActive[bandNum]) {
 	  channel = 0;
	  if (cptr->crate[crate-1].description.pointsPerChunk[band][chunk] > 0) {
	    /* printf("Doing the copy (%d)\n", */
	    /* 	   cptr->crate[crate-1].description.pointsPerChunk[band][chunk]); */
	    while (channel < cptr->crate[crate-1].description.pointsPerChunk[band][chunk]) {
	      int sb;
	      
	      for (sb = 0; sb < N_SIDEBANDS; sb++) {
		cptr->crate[crate-1].data[bsln[band]].amp[band][sb][chunkOffset[band]+channel] =
		  sqrt(dataPointers[band][bsln[band]][chunk]->real.real_val[sb].channel.channel_val[channel]*
		       dataPointers[band][bsln[band]][chunk]->real.real_val[sb].channel.channel_val[channel] +
		       dataPointers[band][bsln[band]][chunk]->imag.imag_val[sb].channel.channel_val[channel]*
		       dataPointers[band][bsln[band]][chunk]->imag.imag_val[sb].channel.channel_val[channel]);
		cptr->crate[crate-1].data[bsln[band]].phase[band][sb][chunkOffset[band]+channel] =
		  atan2(dataPointers[band][bsln[band]][chunk]->imag.imag_val[sb].channel.channel_val[channel],
			dataPointers[band][bsln[band]][chunk]->real.real_val[sb].channel.channel_val[channel]);
	      }
	      channel++;
	    }
	  }
	}
	chunkOffset[band] += cptr->crate[crate-1].description.pointsPerChunk[band][chunk];
      }
      bsln[band]++;
    }
  }
  printf("I'm at the test\n");
  if (debugMessagesOn || 1)
    print_sm_structure(cptr);
  cptr->updating = 0;
  return((statusStructure *)result2);
}

statusStructure *wResult2;
statusStructure *plot_visibilities_1_svc(data, dummy)
pCrateUVBlock *data;
struct svc_req *dummy;
{
  CLIENT *cl;

  printf("In plot_visibilities_1_svc\n");
  wResult2 = plot_visibilities_1(data, cl);
  return((statusStructure *)wResult2);
}

statusStructure *sWARMResult;
statusStructure *plot_swarm_data_1(data, cl)
pSWARMUVBlock *data;
CLIENT *cl;
{
  static int firstCall = TRUE;
  static int baselineMapping[8][8];
  int i, j, ant1, ant2, chunk;

  printf("In plot_swarm_data_1\n");
  cptr->updating = TRUE;
  printf("nChannels = %d\n", data->nChannels);
  if (firstCall) {
    i = 0;
    for (ant1 = 0; ant1 < 7; ant1++)
      for (ant2 = ant1+1; ant2 < 8; ant2++)
	baselineMapping[ant1][ant2] = i++;
    firstCall = FALSE;
  }

  ant1 = data->ant1; ant2 = data->ant2;
  if (ant1 > ant2) {
    i = ant1;
    ant1 = ant2;
    ant2 = i;
  }
  if (ant1 == ant2) {
    /* Auto correlation spectrum sent */
    printf("Saving autocorrelation spectrum from antenna %d\n", ant1);
    for (chunk = 0; chunk < N_SWARM_CHUNKS; chunk++)
      for (i = 0; i < P_N_SWARM_CHANNELS; i++)
	cptr->sWARMAutocorrelation[ant1].amp[chunk][i] = data->lSB[i];
    cptr->sWARMAutocorrelation[ant1].haveAutoData = TRUE;
  } else {
    /* Cross correlation spectrum sent */
    j = baselineMapping[ant1-1][ant2-1];
    printf("Saving cross correlation spectrum for baseline %d-%d (index %d)\n", ant1, ant2, j);
    cptr->sWARMBaseline[j].ant[0] = ant1;
    cptr->sWARMBaseline[j].ant[1] = ant2;
    chunk = data->chunk;
    for (i = 0; i < P_N_SWARM_CHANNELS; i++) {
      float real, imag, amp, phase;
      
      real = data->lSB[2*i];
      imag = data->lSB[2*i + 1];
      amp = sqrt(real*real + imag*imag);
      phase = atan2(imag, real);
      cptr->sWARMBaseline[j].amp[chunk][0][i]   = amp;
      cptr->sWARMBaseline[j].phase[chunk][0][i] = phase;
      real = data->uSB[2*i];
      imag = data->uSB[2*i + 1];
      amp = sqrt(real*real + imag*imag);
      phase = atan2(imag, real);
      cptr->sWARMBaseline[j].amp[chunk][1][i] = amp;
      cptr->sWARMBaseline[j].phase[chunk][1][i] = phase;
      /* printf("%d-%d (%d):%d %d: %e %e  %e %e\n", */
      /* 	     ant1, ant2, j,  chunk, i, cptr->sWARMBaseline[j].amp[chunk][0][i],cptr->sWARMBaseline[j].phase[chunk][0][i], */
      /* 	     cptr->sWARMBaseline[j].amp[chunk][1][i], cptr->sWARMBaseline[j].phase[chunk][1][i]); */
    }
    cptr->sWARMBaseline[j].haveCrossData = TRUE;
    /* printf("Done squirling away %d channels\n", P_N_SWARM_CHANNELS); */
  }
  cptr->sWARMScan++;
  cptr->updating = FALSE;
  printf("Exiting plot_swarm_data_1\n");
  return((statusStructure *)sWARMResult);
}

statusStructure *wResult3;
statusStructure *plot_swarm_data_1_svc(data, dummy)
pSWARMUVBlock *data;
struct svc_req *dummy;
{
  static int firstCall = TRUE;
  CLIENT *cl;

  printf("In plot_swarm_data_1_svc (!)\n");
  if (firstCall) {
    if (cptr == NULL)
      makeSharedMemory();
    sWARMResult = (statusStructure *)malloc(sizeof(*sWARMResult));
    if (sWARMResult == NULL) {
      perror("sWARMResult");
      exit(-1);
    }
    firstCall = FALSE;
  }
  sWARMResult = plot_swarm_data_1(data, cl);
  sWARMResult->rt_code = 0;
  return((statusStructure *)sWARMResult);
}
