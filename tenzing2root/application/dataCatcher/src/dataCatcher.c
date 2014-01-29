/*
  dataCatcher.c
  Rev. 2.0 Started May 6, 2004
  Raoul Taco Machilvich

  This program acts as an RPC server to receive the completed scan data 
  (called a visibility bundle) from the correlator crates, and writes
  them to the mir format data files.   It has three threads:
  1) The SERVER thread which is the RPC server receiving the bundles.
  2) The HEADER thread which fetches scan header information from
  hal9000.
  3) The WRITER thread which writes the scans to the file
  server disk.

  This program is nearly total rewrite of a program, by the same name,
  originally written by Eric Keto.   Some of his original comments have
  been included here.   They are flagged as "Keto Komments".

*/

/*   P R E P R O C E S S O R   C O M A N D S   */

#include <math.h>
#include <bits/nan.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "/global/include/astrophys.h"
#include "/global/include/scanFlags.h"
#include "nominalPadLocations.h"
#include "dataFlags.h"
#include "bandwidthDoubler.h"
/* #include "dsm.h" */
#include "dataCatcher.h"   /* RPC server/client structures                  */
#include "mirStructures.h"
#include "statusServer.h"
#include "setLO.h"
#include "dataDirectoryCodes.h"
#include "blocks.h"

#define N_SWARM_CHUNK_POINTS (16384)
#define MAX_SWARM_CHUNK (2)
#define SWARM_CHUNK_WIDTH (2.0)
#define SWARM_IF_MIDPOINT (9.0)
#define SWARM_CRATE (13)
#define SWARM_BLOCK (7)

#define dprintf if (debugMessagesOn) printf /* Print IFF debugging          */
#define MAX_RX              (2)
#define MAX_CRATE          (13)
#define MAX_ANT            (10)
#define MAX_BASELINE       (MAX_ANT*(MAX_ANT-1)/2)
#define MAX_BLOCK           (6)
#define MAX_INTERIM_CHUNK   (2)
#define MAX_SB              (2) /* Two sidebands */
#define MAX_PENDING_SCANS   (3)
#define MAX_PAD            (26)
#define MAX_SPACELIKE_COORD (3)
#define MAX_POLARIZATION    (4)
#define MAX_SOURCES     (10000) /* I should make this dynamic */
#define UNINITIALIZED      (-1) /* Flags an uninitialized array element */
#define MIDPOINT_SLOP 1.0       /* Bundles differing by less than this amount are
			           considered to be part of the same scan. */

#define LONGRAD                (-2.713594689147) /* pad1 */
#define LATRAD                 (0.345997653446)  /* pad1 */
#define BLOCK_SPACING_HZ       (328.0e6)
#define TWO_PI                 (2.0 * M_PI)
#define DEGREES_TO_RADIANS     (M_PI / 180.0)
#define RADIANS_TO_DEGREES     (180.0 / M_PI)
#define HOURS_TO_RADIANS       (M_PI / 12.0)
#define RADIANS_TO_HOURS       (12.0 / M_PI)
#define CHUNK_FULL_BANDWIDTH   (104.0e6)
#define SWARM_CHUNK_FULL_BANDWIDTH (2.288e9)
#define CHUNK_USABLE_BANDWIDTH (82.0e6)
#define SWARM_CHUNK_USABLE_BANDWIDTH (2.0e9)
#define IS_OFFLINE             (0)
#define ONLINE                 (1)
#define OK                     (0)
#define ERROR                  (-1)
/* processBundle error codes */
#define UNEXPECTED_BUNDLE      (-2)
#define REDUNDANT_BUNDLE       (-3)

#define SERVER_PRIORITY (20)
#define HEADER_PRIORITY (19)
#define WRITER_PRIORITY (18)
#define COPIER_PRIORITY (17)

#define POL_STATE_UNKNOWN (0)
#define POL_STATE_RR      (1)
#define POL_STATE_RL      (2)
#define POL_STATE_LR      (3)
#define POL_STATE_LL      (4)
#define POL_STATE_LH      (5)
#define POL_STATE_LV      (6)
#define POL_STATE_RH      (7)
#define POL_STATE_RV      (8)
#define POL_STATE_HR      (9)
#define POL_STATE_HL      (10)
#define POL_STATE_HH      (11)
#define POL_STATE_HV      (12)
#define POL_STATE_VR      (13)
#define POL_STATE_VL      (14)
#define POL_STATE_VH      (15)
#define POL_STATE_VV      (16)

/*   T Y P E D E F S   */

typedef struct dSMInfo {
  short polarMode;
  short pointingMode;
  char polarStates[12];
} dSMInfo;

typedef struct pendingScan {
  int           expected[MAX_CRATE+1]; /* List of crates expected to report      */
  int           received[MAX_CRATE+1]; /* List of crates that have been received */
  double        firstTime;             /* Time stamp of first bundle             */
  double        intTime;               /* Length of the scan in seconds          */
  double        birthTime;
  int           number;
  int           gotHeaderInfo;         /* Flag we've got data from statusServer  */
  info          header;                /* Stuff from statusServer                */
  dSMInfo       dSMStuff;
  double        chunkFreq[MAX_RX][MAX_SB][(2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK)+1];
  double        chunkVelo[MAX_RX][MAX_SB][(2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK)+1];
  double        sWARMFreq[MAX_SB][MAX_SWARM_CHUNK], sWARMVelo[MAX_SB][MAX_SWARM_CHUNK];
  double        u[MAX_ANT+1][MAX_ANT+1];
  double        v[MAX_ANT+1][MAX_ANT+1];
  double        w[MAX_ANT+1][MAX_ANT+1];
  short         padList[MAX_ANT+1];
  double        padE[MAX_ANT+1];
  double        padN[MAX_ANT+1];
  double        padU[MAX_ANT+1];
  int           hiRes[MAX_CRATE+1];
  int           nDaisyChained[MAX_CRATE+1];
  int           nInDaisyChain[MAX_CRATE+1];
  dCrateUVBlock *data[MAX_CRATE+1];    /* Cached copy of UV data bundles         */
  char *last;
  char *next;
} pendingScan;

typedef struct crateSetIndex {
  short crate;
  short set;
} crateSetIndex;

typedef struct baselineIndex {
  short sb;
  short ant1;
  short ant2;
  short code;
} baselineIndex;

/*   G L O B A L   V A R I A B L E S   */

typedef struct sWARMAutoRec {
  autoCorrDef autoData;
  struct sWARMAutoRec *last;
  struct sWARMAutoRec *next;
} sWARMAutoRec;
sWARMAutoRec *sWARMAutoRoot = NULL;

double sWARMCenterFrequency;
short goodChunk[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][(2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK)+1];
int antennaInArrayInitialized = FALSE;
int antennaInArray[11];
int activeCrates[MAX_CRATE+1];
int abortOnMinorErrors = FALSE; /* Abort on detection of errors even if recoverable */
int debugMessagesOn = FALSE;
int needHeader      = FALSE; /* Used with condition variable to activate header thread */
int needWrite       = FALSE; /* Used with condition variable to activate writer thread */
int doDSMWrite      = FALSE;  /* Turn on or off the writing of DSM variables            */
int needNewDataFile = TRUE;
int safeRestarts    = TRUE;  /* If TRUE, abort on HUP signal rather than trying to     */
                             /* initialize.                                            */
extern int fullPolarization;
extern int doubleBandwidth; /* This gets set TRUE in when both IFs are used for a single receiver */
int doubleBandwidthRx;
double bDAIFSep = 2.0e9; /* Frequency separtion of the IFs in double bandwidth mode */
short doubleBandwidthContinuum = FALSE;
int doubleBandwidthOffset = 0; /* This holds the offset, usually 24 between lower 2 GHz and
				  upper 2 GHz chunks in double bandwidth mode. */
int sendOperatorMessages = TRUE;
int haveLOData = FALSE;
int missingCrate = 0;
int reportAllErrors = FALSE;
int globalScanNumber = 0;
int foundAntennaList[MAX_ANT+1];
int receiverActive[MAX_RX+1] = {FALSE, FALSE, FALSE};
int chunkCodes[MAX_RX+1][2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK+1];
int nSources = 0;
int nChunkCodes = 1;
long refJD = 0;
int nBaselineCodes = 0;
int nAntennas = 0;
int iRefTime = -1;
int store = TRUE;
char pathName[80];          /* path for directory where data is stored      */
char globalSourceName[35];
int spoilScanFlag = FALSE;
double globalSkyFrequency[2];
struct frequenciesDef globalFrequencies;

blhDef blh[MAX_RX][MAX_SB][2*MAX_BASELINE];
pendingScan *scanRoot = NULL;
pendingScan *headerScan = NULL; /* Points to scan needing header info */
pendingScan *writableScan = NULL; /* Points to completed scan */
crateSetIndex cSIndx[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_POLARIZATION][2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
baselineIndex bslnIndx[MAX_SIDEBAND*MAX_BASELINE];
/*
  bandIndx holds the s-format (1..24) chunk number for each existing chunk for each
  receiver.   The main purpose of this array is to handle 0 channel/chunk chunks,
  which do not appear at all in the incoming bundle or the MIR file, so they must be skipped
  some how.   This array allows you to count through however many chunks actually exist,
  and hop over the chunks that don't exist in this configuration.   So you can loop over
  the number of existing chunks sequentially.
 */
int bandIndx[MAX_RX+1][2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
int chunkSName[MAX_RX+1][2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
int chunkSNameInitialized = FALSE;

/*   T H R E A D   S T U F F */

pthread_t headerTId, writerTId, copierTId;

/*   M U T E X E S   */

pthread_mutex_t scanMutex = PTHREAD_MUTEX_INITIALIZER; /* Protects the linked list of scans */
pthread_mutex_t needHeaderMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t writeScanMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t autoMutex = PTHREAD_MUTEX_INITIALIZER; /* Protects linked list of autocorrelations */

/*   C O N D I T I O N   V A R I A B L E S   */

pthread_cond_t needHeaderCond = PTHREAD_COND_INITIALIZER;
pthread_cond_t writeScanCond = PTHREAD_COND_INITIALIZER;

/*   F U N C T I O N   P R O T O T Y P E S   */

extern int getCrateList(int *members);
extern int getAntennaList(int *members);

/* Prototypes for functions in novas.c */
void sidereal_time (double jd_high, double jd_low, double ee,
                    double *gst);
void earthtilt (double tjd, 
                double *mobl, double *tobl, double *eq, double *dpsi,
                double *deps);

/*-------------------------------------------*/
/*                                           */
/*   E N D   O F   D E C L A R A T I O N S   */
/*                                           */
/*-------------------------------------------*/

/*

  I N I T I A L I Z E   A R R A Y S

  initializeArrays resets all the arrays and counters to the values
  they should have before the first scan is stored.
*/

void initializeArrays(void)
{
  int i, ant1, ant2, band, rx, pol;

  iRefTime = -1;
  globalScanNumber = nSources = nBaselineCodes = nAntennas = refJD = 0;
  nChunkCodes = 1;
  for (rx = 0; rx < MAX_RX+1; rx++)
    for (ant1 = 0; ant1 < MAX_ANT+1; ant1++)
      for (ant2 = 0; ant2 < MAX_ANT+1; ant2++)
	for (pol = 0; pol < MAX_POLARIZATION; pol++)
	  for (band = 0; band < MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1; band++) {
	    cSIndx[rx][ant1][ant2][pol][band].crate = UNINITIALIZED;
	    cSIndx[rx][ant1][ant2][pol][band].set = UNINITIALIZED;
	}
  for (i = 0; i < MAX_BASELINE*MAX_SIDEBAND; i++)
    bslnIndx[i].code = UNINITIALIZED;
  for (rx = 0; rx < MAX_RX+1; rx++)
    for (i = 0; i < 2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1; i++)
      chunkCodes[rx][i] = UNINITIALIZED;
  for (i = 0; i < MAX_ANT+1; i++)
    foundAntennaList[i] = UNINITIALIZED;
}

/*
  S I D E B A N D   S I G N

    sidebandSign takes as its only argument a chunk number (s-style)
    and returns 1.0 it the number of LSB downconversions for that
    chunk is even, and -1.0 otherwise.
*/
double sidebandSign(int rx, int chunk)
{
  double sign;

  switch (chunk) {
  case  1: case  2: case  5: case  6: case  9: case 10:
  case 13: case 14: case 17: case 18: case 21: case 22:
    sign =  1.0;
    break;
  default:
    sign = -1.0;
  }
  if (rx == 1)
    sign *= -1.0;
  return(sign);
}

/*
  S   C H U N K

  sChunk returns the mir-format chunk for a block, chunk pair.
  These chunks are numbered 1 -> 24.
*/
int sChunk(int block, int chunk)
{
  if (block == SWARM_BLOCK) {
    dprintf("sChunk called with SWARM Block (%d), chunk = %d returning %d\n", block, chunk, 48+chunk);
    return (48+chunk);
  } else if (block < 4)
    return((block-1)*4 + chunk);
  else
    return(block*4 - chunk + 1);
} /* End of sChunk */

/*
  H I G H   S   C H U N K

  highSChunk returns the chunk number for a block/chunk combo in the
  upper 2 GHz when in double bandwidth mode.
 */
int highSChunk(int block, int chunk)
{
  if (block == SWARM_BLOCK) {
    dprintf("highSChunk called SWARM Block (%d), returning %d\n", block, 48+chunk);
    return(48+chunk);
  } if (block < 4)
    return(49-((block-1)*4 + chunk));
  else
    return(49-(block*4 - chunk + 1));
} /* End of highSChunk */

/*
  F L A G   C H U N K   B A D

  flagChunkBad flags a particular chunk as bad on all baselines and
  receivers.   This means it will not be included in calculating the
  pseudo-continuum channel.
*/
void flagChunkBad(int block, int chunk)
{
  int rx, a1, a2;

  for (rx = 0; rx <= MAX_RX; rx++)
    for (a1 = 0; a1 <= MAX_ANT; a1++)
      for (a2 = 0; a2 <= MAX_ANT; a2++)
	goodChunk[rx][a1][a2][sChunk(block, chunk)] = FALSE;
}

/*
  B U N D L E   C O P Y

  bundleCopy makes a copy of the portions of a bundle we actually need
*/
void bundleCopy(dCrateUVBlock *source, dCrateUVBlock **dest, int interpret,
		int *hiRes, int *nDaisyChained, int *nInDaisyChain)
{
  int set, hiResCount, hiResPtr, nSets;

  *hiRes = *nDaisyChained = *nInDaisyChain = 0;
  (*dest) = (dCrateUVBlock *)malloc(sizeof(*((*dest))));
  if ((*dest) == NULL) {
    perror("processScan - first bundle copy malloc");
    exit(ERROR);
  }
  (*dest)->crateNumber = source->crateNumber;
  (*dest)->blockNumber = source->blockNumber;
  (*dest)->scanType    = source->scanType;
  (*dest)->scanNumber  = source->scanNumber;
  (*dest)->UTCtime     = source->UTCtime;
  (*dest)->intTime     = source->intTime;
  (*dest)->set.set_len = source->set.set_len;
  strcpy((*dest)->sourceName, source->sourceName);
  hiResCount = 1;
  /*
    Since the source name in the bundle of data from the crate actually does not have the
    real source name in it (ever), it is used to send auxilary processing information.
    Check here to see if the crate is using that field to tell us we're in HiRes mode
    (daisy-chained crates).
  */
  if (strstr(source->sourceName, "Hi-Res") != NULL) {
    int nRead;
    
    printf("**** Saw the Hi-Res switch! ****\n");
    nRead = sscanf(source->sourceName, "Hi-Res %d %d", nDaisyChained, nInDaisyChain);
    if (nRead == 2) {
      *hiRes = TRUE;
      if ((*nInDaisyChain == 1) && interpret) {
	hiResCount = 2;
      } else if (*nInDaisyChain > 1) {
	/*
	  Flag the chunks containing partial Hi-Res products bad, for purposes
	  of calculating the pseudo-continuum channel.
	*/
	dprintf("Flagging chunk s%02d bad\n", sChunk(source->blockNumber, 1));
	flagChunkBad(source->blockNumber, 1);
      }
    } else
      *hiRes = *nDaisyChained = *nInDaisyChain = 0;
  }
  if (*hiRes && interpret && (*nInDaisyChain == 1)) {
    printf("The bundle from crate %d needs an additional chunk added for Hi-Res\n",
	    source->crateNumber);
    printf("Flagging chunk s%02d bad\n", sChunk(source->blockNumber, 2));
    flagChunkBad(source->blockNumber, 2);
    (*dest)->set.set_len = 2 * source->set.set_len;
    (*dest)->set.set_val =
      (dVisibilitySet *)malloc((source->set.set_len * 2) * sizeof(dVisibilitySet));
  } else
    (*dest)->set.set_val =
      (dVisibilitySet *)malloc(source->set.set_len * sizeof(dVisibilitySet));
  if ((*dest)->set.set_val == NULL) {
    perror("processScan - second bundle copy malloc");
    exit(ERROR);
  }

  hiResPtr = 0;
  nSets = source->set.set_len;
  do {
    for (set = 0; set < nSets; set++) {
      int ii, len;
      
      (*dest)->set.set_val[hiResPtr*nSets + set].chunkNumber = 
	source->set.set_val[set].chunkNumber + hiResPtr;
      for (ii = 0; ii < 3; ii++) {
	(*dest)->set.set_val[hiResPtr*nSets + set].antennaNumber[ii] =
	  source->set.set_val[set].antennaNumber[ii];
	(*dest)->set.set_val[hiResPtr*nSets + set].antPolState[ii] =
	  source->set.set_val[set].antPolState[ii];
	if (*hiRes && interpret && (ii == 1))
	  dprintf("hiResPtr %d, set %d\t(*dest)->set.set_val[%d].chunkNumber = %d\n",
		  hiResPtr, set, hiResPtr*nSets,
		  (*dest)->set.set_val[hiResPtr*nSets + set].chunkNumber);
	if (*hiRes && interpret && (ii != 0))
	  dprintf("hiResPtr %d, set %d\t(*dest)->set.set_val[%d].antennaNumber[%d] = %d\n",
		  hiResPtr, set, hiResPtr*nSets, ii,
		  (*dest)->set.set_val[hiResPtr*nSets + set].antennaNumber[ii]);
      }
      (*dest)->set.set_val[hiResPtr*nSets + set].rxBoardHalf = source->set.set_val[set].rxBoardHalf;
      /*   R E A L    P A R T   */
      (*dest)->set.set_val[hiResPtr*nSets + set].real.real_len = source->set.set_val[set].real.real_len;
      (*dest)->set.set_val[hiResPtr*nSets + set].real.real_val =
	(dVarArray *)malloc(source->set.set_val[set].real.real_len * sizeof(dVarArray));
      if ((*dest)->set.set_val[hiResPtr*nSets + set].real.real_val == NULL) {
	perror("processScan - third bundle copy malloc");
	exit(ERROR);
      }
      /* real_len = number of sidebands (usually 2) */
      for (len = 0; len < source->set.set_val[set].real.real_len; len++) {
	(*dest)->set.set_val[hiResPtr*nSets + set].real.real_val[len].channel.channel_len =
	  source->set.set_val[set].real.real_val[len].channel.channel_len;
	if (*hiRes && interpret)
	  dprintf("Chunk size: %d\n", source->set.set_val[set].real.real_val[len].channel.channel_len);
	(*dest)->set.set_val[hiResPtr*nSets + set].real.real_val[len].channel.channel_val =
	  (float *)malloc(source->set.set_val[set].real.real_val[len].channel.channel_len * sizeof(float));
	if ((*dest)->set.set_val[hiResPtr*nSets + set].real.real_val[len].channel.channel_val == NULL) {
	  perror("processScan - fourth bundle copy malloc");
	  exit(ERROR);
	}
	/* channel_len = number of points in spectrum */
	bcopy((char *)source->set.set_val[set].real.real_val[len].channel.channel_val,
	      (char *)(*dest)->set.set_val[hiResPtr*nSets + set].real.real_val[len].channel.channel_val,
	      source->set.set_val[set].real.real_val[len].channel.channel_len * sizeof(float));
      }
      
      /*   I M A G I N A R Y    P A R T   */
      (*dest)->set.set_val[hiResPtr*nSets + set].imag.imag_len = source->set.set_val[set].imag.imag_len;
      (*dest)->set.set_val[hiResPtr*nSets + set].imag.imag_val =
	(dVarArray *)malloc(source->set.set_val[set].imag.imag_len * sizeof(dVarArray));
      if ((*dest)->set.set_val[hiResPtr*nSets + set].imag.imag_val == NULL) {
	perror("processScan - fifth bundle copy malloc");
	exit(ERROR);
      }
      /* imag_len = number of sidebands (usually 2) */
      for (len = 0; len < source->set.set_val[set].imag.imag_len; len++) {
	(*dest)->set.set_val[hiResPtr*nSets + set].imag.imag_val[len].channel.channel_len =
	  source->set.set_val[set].imag.imag_val[len].channel.channel_len;
	(*dest)->set.set_val[hiResPtr*nSets + set].imag.imag_val[len].channel.channel_val =
	  (float *)malloc(source->set.set_val[set].imag.imag_val[len].channel.channel_len * sizeof(float));
	if ((*dest)->set.set_val[hiResPtr*nSets + set].imag.imag_val[len].channel.channel_val == NULL) {
	  perror("processScan - sixth bundle copy malloc");
	  exit(ERROR);
	}
	/* channel_len = number of points in spectrum */
	bcopy((char *)source->set.set_val[set].imag.imag_val[len].channel.channel_val,
	      (char *)(*dest)->set.set_val[hiResPtr*nSets + set].imag.imag_val[len].channel.channel_val,
	    source->set.set_val[set].imag.imag_val[len].channel.channel_len * sizeof(float));
      }
    }
    hiResPtr++;
  } while (hiResPtr < hiResCount);
} /* End of bundleCopy */

/*

  D E L E T E  S C A N

  deleteScan removes a scan from the scan list, and frees up
  its resources (malloc'd memory).
*/
void deleteScan(pendingScan *victim, int pointer)
{
  int i;
  struct timespec now;

  clock_gettime(CLOCK_REALTIME, &now);
  if (pointer) {
    if (victim->last != NULL)
      ((pendingScan *)victim->last)->next = victim->next;
    else
      scanRoot = (pendingScan *)victim->next;
    if (victim->next != NULL)
      ((pendingScan *)victim->next)->last = victim->last;
  }
  for (i = 0; i <= MAX_CRATE; i++)
    if (victim->data[i] != NULL) {
      int set;

      for (set = 0; set < victim->data[i]->set.set_len; set++) {
	int len;

	for (len = 0; len < victim->data[i]->set.set_val[set].real.real_len; len++)
	  free(victim->data[i]->set.set_val[set].real.real_val[len].channel.channel_val);
	for (len = 0; len < victim->data[i]->set.set_val[set].imag.imag_len; len++)
	  free(victim->data[i]->set.set_val[set].imag.imag_val[len].channel.channel_val);
	free(victim->data[i]->set.set_val[set].real.real_val);
	free(victim->data[i]->set.set_val[set].imag.imag_val);
      }
      free(victim->data[i]->set.set_val);
      free(victim->data[i]);
    }
  if (pointer)
    free(victim);
} /* End of deleteScan */

/*

  M A K E   S C A N

  makeScan creates a new scan list entry, initializes it,
  trims the scan list if there are too many pending scans,
  and inserts the new scan into the queue.

  The scanMutex must be acquired before this function is called.
*/

void makeScan(pendingScan **newEntry, dCrateUVBlock *bundle)
{
  int i, scanCount;
  double oldestScanTime;
  struct timespec birthTime;
  pendingScan *pointer, *lastScan;
  pendingScan *oldestScan = NULL;

  clock_gettime(CLOCK_REALTIME, &birthTime);
  /* Make new entry */
  *newEntry = (pendingScan *)malloc(sizeof(**newEntry));
  if (*newEntry == NULL) {
    fprintf(stderr, "makeScan: malloc failure for newEntry - will abort\n");
    perror("makeScan, newEntry");
    exit(ERROR);
  }
  
  /* Initialize new entry */
  dprintf("makeScan:\tInitializing the new entry\n");
  (*newEntry)->firstTime = bundle->UTCtime;
  (*newEntry)->birthTime = ((double)birthTime.tv_sec) + ((birthTime.tv_nsec))*1.0e-9;
  (*newEntry)->number = globalScanNumber;
  (*newEntry)->intTime = bundle->intTime;
  for (i = 0; i <= MAX_CRATE; i++) {
    if (activeCrates[i])
      (*newEntry)->expected[i] = TRUE;
    else
      (*newEntry)->expected[i] = FALSE;
    (*newEntry)->received[i] = FALSE;
    (*newEntry)->data[i] = NULL;
  }
  (*newEntry)->gotHeaderInfo = FALSE;
  (*newEntry)->next = NULL; /* We'll put it at the end of the list */

  /*
    Let's count the number of pending scans, note the oldest,
    and delete it if too many scans are pending.
  */
  scanCount = 0;
  pointer = scanRoot;
  /*
    Delete any really old pending scans
  */
  while (pointer != NULL) {
    if ((globalScanNumber - pointer->number) > 10) {
      printf("Dropping ancient scan %d\n", pointer->number);
      deleteScan(pointer, TRUE);
      pointer = NULL;
    } else
      pointer = (pendingScan *)pointer->next;
  }
  pointer = scanRoot;
  oldestScanTime = 1.0e30;
  while (pointer != NULL) {
    scanCount++;
    if (pointer->firstTime < oldestScanTime) {
      oldestScan = pointer;
      oldestScanTime = pointer->firstTime;
    }
    pointer = (pendingScan *)pointer->next;
  }
  if (scanCount >= MAX_PENDING_SCANS) {
    fprintf(stderr, "makeScan: Too many pending scans (%d), will drop oldest\n",
	    MAX_PENDING_SCANS);
    fprintf(stderr, "\tOldest time: %f\n", oldestScanTime);
    fprintf(stderr, "\tExpected crates: ");
    for (i = 1; i <= MAX_CRATE; i++)
      if (oldestScan->expected[i])
	fprintf(stderr, "%2d ", i);
    fprintf(stderr, "\n");
    fprintf(stderr, "\tReceived crates: ");
    for (i = 1; i <= MAX_CRATE; i++)
      if (oldestScan->received[i])
	fprintf(stderr, "%2d ", i);
    fprintf(stderr, "\n");
    deleteScan(oldestScan, TRUE);
    if (abortOnMinorErrors)
      if (globalScanNumber > 100)
	exit(-1);
  }

  /* Now insert the new entry into the doubly linked list */
  pointer = lastScan = scanRoot;
  while (pointer != NULL) {
    lastScan = pointer;
    pointer = (pendingScan *)pointer->next;
  }
  if (lastScan == NULL) {
    dprintf("Inserting new scan with time %f at list root\n",
	    (*newEntry)->firstTime);
    scanRoot = *newEntry;
    (*newEntry)->last = NULL;
  } else {
    dprintf("Inserting new scan with time %f at list end.  %d scans now pending\n",
	    (*newEntry)->firstTime, scanCount+1);
    lastScan->next = (char *)*newEntry;
    (*newEntry)->last = (char *)lastScan;
  }
  /*
    Now signal the HEADER thread that we need a header for this new scan
  */
  pthread_mutex_unlock(&scanMutex);
  pthread_mutex_lock(&needHeaderMutex);
  needHeader = TRUE;
  headerScan = *newEntry;
  pthread_cond_signal(&needHeaderCond);
  pthread_mutex_unlock(&needHeaderMutex);
} /* End of makeScan */

/*

   S C A N   C O M P L E T E   C H E C K

   This function checks to see if the scan it has been passed is
   complete, meaning that it contains all the required visibility
   bundles, and the header information.
*/
void scanCompleteCheck(pendingScan *current)
{
  int i, missingScan;

  missingScan = FALSE;
  missingCrate = 0;
  for (i = 1; (i <= MAX_CRATE) && (!missingScan); i++) {
    dprintf("\tSCC: %d expected %d received %d missing %d\n", i, current->expected[i], current->received[i], missingScan);
    if (current->expected[i] && (!current->received[i])) {
      missingScan = TRUE;
      missingCrate = i;
    }
  }
  printf("missingScan = %d, crate = %d, gotHeader = %d\n", missingScan, missingCrate, current->gotHeaderInfo);

  /*
    If there are no missing scans, and the header info
    is present, then signal the WRITER
    thread that there is a scan waiting for processing
  */
  dprintf("In scanCompleteCheck, missing = %d, gotHeader = %d\n", missingScan, current->gotHeaderInfo);
  if ((!missingScan) && current->gotHeaderInfo) {
    pthread_mutex_lock(&writeScanMutex);
    needWrite = TRUE;
    writableScan = current;
    pthread_cond_signal(&writeScanCond);
    pthread_mutex_unlock(&writeScanMutex);
  }
} /* End of scanCompleteCheck */

/*
  P R I N T   B U N D L E   I N F O

  Just used to print debugging information.
 */
void printBundleInfo(dCrateUVBlock *bundle)
{
  int set;

  printf("Bundle Crate%d block %d, source \"%s\", type %d, #%d, UTC %f int %f\n",
	 bundle->crateNumber, bundle->blockNumber, bundle->sourceName,
	 bundle->scanType, bundle->scanNumber, bundle->UTCtime, bundle->intTime);
  printf("set_len = %d\n", bundle->set.set_len);
  for (set = 0; set < bundle->set.set_len; set++) {
    printf("set %d, real_len: %d, imag_len: %d, half: %d chunk: %d %d-%d\n", set,
	   bundle->set.set_val[set].real.real_len,
	   bundle->set.set_val[set].imag.imag_len,
	   bundle->set.set_val[set].rxBoardHalf,
	   bundle->set.set_val[set].chunkNumber,
	   bundle->set.set_val[set].antennaNumber[1],
	   bundle->set.set_val[set].antennaNumber[2]
	   );
  }
} /* End of printBundleInfo */

/*

  P R O C E S S   B U N D L E

  processBundle is the main function for the SERVER thread
*/
int  processBundle(dCrateUVBlock *bundle)
{
  int crate;
  pendingScan *current;

  getCrateList(&activeCrates[0]);

  if (debugMessagesOn)
    printBundleInfo(bundle);

  crate = bundle->crateNumber;
  pthread_mutex_lock(&scanMutex);
  if (scanRoot == NULL) {
    makeScan(&current, bundle);
    pthread_mutex_lock(&scanMutex);
  } else {
    int foundMatch = FALSE;

    /*
      Find out if the midpoint time for this scan matches any
      of the pending scans.
    */
    current = scanRoot;
    while ((current != NULL) && (!foundMatch)) {
      if ((fabs(current->firstTime - bundle->UTCtime) > MIDPOINT_SLOP) && (crate != SWARM_CRATE)) {
	current = (pendingScan *)current->next;
      } else {
	/* This bundle belongs in a currently pending scan */
	if (!(current->expected[crate])) {
	  fprintf(stderr,
		  "processBundle: Got a bundle from crate %d, but no scan from\n",
		  bundle->crateNumber);
	  fprintf(stderr,
		  "               that crate was expected - will discard this bundle.\n");
	  fprintf(stderr,
		  "               First time = %f, bundle time = %f\n",
		  current->firstTime, bundle->UTCtime);
	  pthread_mutex_unlock(&scanMutex);
	  return(UNEXPECTED_BUNDLE);
	} else if (current->received[crate]) {
	  fprintf(stderr,
		  "processBundle: Got a bundle from crate %d, but the current scan\n",
		  bundle->crateNumber);
	  fprintf(stderr,
		  "               already has a bundle from that crate - will discard\n");
	  fprintf(stderr,
		  "               this bundle.   First time = %f, bundle time = %f\n",
		  current->firstTime, bundle->UTCtime);
	  pthread_mutex_unlock(&scanMutex);
	  return(REDUNDANT_BUNDLE);
	}
	foundMatch = TRUE;
      }
    }
    if (!foundMatch) {
      /*
	We went through the entire list of scans, but didn't find one with a
	matching midpoint time.   Must make a new scan.
      */
      makeScan(&current, bundle);
      pthread_mutex_lock(&scanMutex);
    }
  }

  /*
    At this point "current" should point to a valid scan, which needs the
    current bundle's data. So, copy the data into the proper slot.
  */
  current->received[crate] = TRUE;
  bundleCopy(bundle, &(current->data[crate]), TRUE,
	     &(current->hiRes[crate]), &(current->nDaisyChained[crate]),
	     &(current->nInDaisyChain[crate]));
  scanCompleteCheck(current);
  pthread_mutex_unlock(&scanMutex);
  return(OK);
} /* End of processBundle */

/*

  U T 2 L S T

  uT2LST calculates the LST from the julian day number and
  the number of seconds past 00:00:00
*/
void uT2LST(long julianday, double secs, double *lst)
{
  
  double tjd_upper, tjd_lower, dpsi, deps, tobl, equinoxes;
  double d1, mobl, gst, lst_radian;
  
  tjd_lower = secs/24.0/3600.0;
  tjd_upper = julianday - 0.5; 
  
  earthtilt(tjd_upper, &mobl, &tobl, &equinoxes, &dpsi, &deps);
  sidereal_time(tjd_upper, tjd_lower, equinoxes, &gst);
  d1 = gst * TWO_PI / 24.0 + LONGRAD;
  lst_radian = fmod(d1, TWO_PI);
  if (lst_radian < 0.0)
    lst_radian += TWO_PI;
  *lst = lst_radian;                                                      
} /* End of uT2LST */

/*

  C A L C U L A T E  U V W

  calculateUVW calculates the U, V and W coordinates for a particular
  time, which will typically be the average (over crates) midpoint
  time of the scan.   Also calculate the pad coordinates in the
  local frame, for use by mir.

  Returns the hour angle at the scan midpoint.
*/
double calculateUVW(pendingScan *scan, double mPTime, long jD)
{
  int ant1, ant2;
  double sHA, cHA, sDec, cDec, sLat, cLat;
  double hourAngle, lST;

  uT2LST(jD, mPTime * 3600.0, &lST);
  hourAngle = lST - scan->header.DDSdata.ra;
  while (hourAngle < -M_PI)
    hourAngle += TWO_PI;
  while (hourAngle > M_PI)
    hourAngle -= TWO_PI;
  sHA = sin(hourAngle);
  cHA = cos(hourAngle);
  sDec = sin(scan->header.DDSdata.dec);
  cDec = cos(scan->header.DDSdata.dec);
  sLat = sin(LATRAD);
  cLat = cos(LATRAD);
  dprintf("LST = %f, RA = %f, HA = %f\n",
	  lST, scan->header.DDSdata.ra, hourAngle);
  for (ant1 = 1; ant1 < MAX_ANT+1; ant1++) {
    scan->padE[ant1] =  scan->header.DDSdata.y[ant1];
    scan->padN[ant1] = -sLat*scan->header.DDSdata.x[ant1] + cLat*scan->header.DDSdata.z[ant1];
    scan->padU[ant1] =  cLat*scan->header.DDSdata.x[ant1] + sLat*scan->header.DDSdata.z[ant1];
    for (ant2 = 1; ant2 < MAX_ANT+1; ant2++)
      if (ant2 > ant1) {
	double X, Y, Z;

	X = scan->header.DDSdata.x[ant1] - scan->header.DDSdata.x[ant2];
	Y = scan->header.DDSdata.y[ant1] - scan->header.DDSdata.y[ant2];
	Z = scan->header.DDSdata.z[ant1] - scan->header.DDSdata.z[ant2];
	scan->u[ant1][ant2] =       sHA*X +      cHA*Y;
	scan->v[ant1][ant2] = -cHA*sDec*X + sHA*sDec*Y + cDec*Z;
	scan->w[ant1][ant2] =  cHA*cDec*X - sHA*cDec*Y + sDec*Z;
	scan->u[ant2][ant1] = scan->u[ant1][ant2];
	scan->v[ant2][ant1] = scan->v[ant1][ant2];
	scan->w[ant2][ant1] = scan->w[ant1][ant2];
      }
  }
  return(hourAngle);
} /* End of calculateUVW */

/*

  C A L C U L A T E  C H U N K  F R E Q U E N C I E S

  calculateChunkFrequencies calculates the center frequency and velocity
  for each chunk.

*/
void calculateChunkFrequencies(pendingScan **scan)
{
  int rx, sb, block, chunk, chunkIndex, effectiveRx;
  double alpha, beta, vRadial, fRest, vCatalog;

  vRadial = (*scan)->header.loData.vRadial;
  vCatalog = (*scan)->header.loData.vCatalog;
  for (rx = 0; rx < MAX_RX; rx++) {
    if (doubleBandwidth)
      effectiveRx = doubleBandwidthRx;
    else
      effectiveRx = rx;
    fRest = (*scan)->header.loData.restFrequency[effectiveRx];
    for (sb = 0; sb < MAX_SB; sb++) {
      for (block = 1; block < MAX_BLOCK+1; block++)
	for (chunk = 1; chunk < MAX_CHUNK+1; chunk++) {

	  chunkIndex = chunk;
	  /* First, calculate the chunk center frequencies */
	  if ((rx == 0) || (!doubleBandwidth)) {
	    if (sb == 0) /* LSB */
	      (*scan)->chunkFreq[rx][sb][sChunk(block,chunkIndex)] =
		(*scan)->header.loData.frequencies.receiver[effectiveRx].sideband[sb].block[block-1].chunk[chunk-1].centerfreq;
	    else         /* USB */
	      (*scan)->chunkFreq[rx][sb][sChunk(block,chunkIndex)] =
		(*scan)->header.loData.frequencies.receiver[effectiveRx].sideband[sb].block[block-1].chunk[chunk-1].centerfreq;
	    /* Next, calculate the chunk center velocities */
	    {
	      int ii, jj, kk;
	      double sign;
	      
	      for (ii = 0; ii < 2; ii++) {
		for (jj = 0; jj < 2; jj++) {
		  if (jj == 0)
		    sign = -1.0;
		  else
		    sign = 1.0;
		  for (kk = 0; kk < 2; kk++) {
		    (*scan)->chunkFreq[ii][jj][ 1] = 230.538e9 + sign*4057.0e6;
		    (*scan)->chunkFreq[ii][jj][ 2] = 230.538e9 + sign*4139.0e6;
		    (*scan)->chunkFreq[ii][jj][ 3] = 230.538e9 + sign*4221.0e6;
		    (*scan)->chunkFreq[ii][jj][ 4] = 230.538e9 + sign*4303.0e6;
		    (*scan)->chunkFreq[ii][jj][ 5] = 230.538e9 + sign*4385.0e6;
		    (*scan)->chunkFreq[ii][jj][ 6] = 230.538e9 + sign*4467.0e6;
		    (*scan)->chunkFreq[ii][jj][ 7] = 230.538e9 + sign*4549.0e6;
		    (*scan)->chunkFreq[ii][jj][ 8] = 230.538e9 + sign*4631.0e6;
		    (*scan)->chunkFreq[ii][jj][ 9] = 230.538e9 + sign*4713.0e6;
		    (*scan)->chunkFreq[ii][jj][10] = 230.538e9 + sign*4795.0e6;
		    (*scan)->chunkFreq[ii][jj][11] = 230.538e9 + sign*4877.0e6;
		    (*scan)->chunkFreq[ii][jj][12] = 230.538e9 + sign*4959.0e6;
		    (*scan)->chunkFreq[ii][jj][13] = 230.538e9 + sign*5041.0e6;
		    (*scan)->chunkFreq[ii][jj][14] = 230.538e9 + sign*5123.0e6;
		    (*scan)->chunkFreq[ii][jj][15] = 230.538e9 + sign*5205.0e6;
		    (*scan)->chunkFreq[ii][jj][16] = 230.538e9 + sign*5287.0e6;
		    (*scan)->chunkFreq[ii][jj][17] = 230.538e9 + sign*5369.0e6;
		    (*scan)->chunkFreq[ii][jj][18] = 230.538e9 + sign*5451.0e6;
		    (*scan)->chunkFreq[ii][jj][19] = 230.538e9 + sign*5533.0e6;
		    (*scan)->chunkFreq[ii][jj][20] = 230.538e9 + sign*5615.0e6;
		    (*scan)->chunkFreq[ii][jj][21] = 230.538e9 + sign*5697.0e6;
		    (*scan)->chunkFreq[ii][jj][22] = 230.538e9 + sign*5779.0e6;
		    (*scan)->chunkFreq[ii][jj][23] = 230.538e9 + sign*5861.0e6;
		    (*scan)->chunkFreq[ii][jj][24] = 230.538e9 + sign*5943.0e6;
		  }
		  (*scan)->chunkFreq[ii][jj][49] = 230.538e9 + sign*9.0e9;
		  (*scan)->chunkFreq[ii][jj][50] = 230.538e9 + sign*11.0e9;
		}
	      }
	    }
	    
	    alpha =  (*scan)->chunkFreq[rx][sb][sChunk(block,chunk)] /
	      (*scan)->header.loData.restFrequency[effectiveRx];
	    alpha *= alpha;
	    beta = (1.0 - alpha)/(1.0 + alpha);
	    (*scan)->chunkVelo[rx][sb][sChunk(block,chunk)] = 0.0;
	  } else {
	    int uChunk;

	    uChunk = 25-sChunk(block, chunkIndex);
	    if (sb == 0) /* LSB */
	      (*scan)->chunkFreq[rx][sb][uChunk] =
		(*scan)->header.loData.frequencies.receiver[effectiveRx].sideband[sb].block[block-1].chunk[chunk-1].centerfreq -
		bDAIFSep;
	    else         /* USB */
	      (*scan)->chunkFreq[rx][sb][uChunk] =
		(*scan)->header.loData.frequencies.receiver[effectiveRx].sideband[sb].block[block-1].chunk[chunk-1].centerfreq +
	        bDAIFSep;
	    /* Next, calculate the chunk center velocities */
	    {
	      int ii, jj, kk;
	      double sign;
	      
	      for (ii = 0; ii < 2; ii++) {
		for (jj = 0; jj < 2; jj++) {
		  if (jj == 0)
		    sign = -1.0;
		  else
		    sign = 1.0;
		  for (kk = 0; kk < 2; kk++) {
		    (*scan)->chunkFreq[ii][jj][ 1] = 230.538e9 + sign*4057.0e6;
		    (*scan)->chunkFreq[ii][jj][ 2] = 230.538e9 + sign*4139.0e6;
		    (*scan)->chunkFreq[ii][jj][ 3] = 230.538e9 + sign*4221.0e6;
		    (*scan)->chunkFreq[ii][jj][ 4] = 230.538e9 + sign*4303.0e6;
		    (*scan)->chunkFreq[ii][jj][ 5] = 230.538e9 + sign*4385.0e6;
		    (*scan)->chunkFreq[ii][jj][ 6] = 230.538e9 + sign*4467.0e6;
		    (*scan)->chunkFreq[ii][jj][ 7] = 230.538e9 + sign*4549.0e6;
		    (*scan)->chunkFreq[ii][jj][ 8] = 230.538e9 + sign*4631.0e6;
		    (*scan)->chunkFreq[ii][jj][ 9] = 230.538e9 + sign*4713.0e6;
		    (*scan)->chunkFreq[ii][jj][10] = 230.538e9 + sign*4795.0e6;
		    (*scan)->chunkFreq[ii][jj][11] = 230.538e9 + sign*4877.0e6;
		    (*scan)->chunkFreq[ii][jj][12] = 230.538e9 + sign*4959.0e6;
		    (*scan)->chunkFreq[ii][jj][13] = 230.538e9 + sign*5041.0e6;
		    (*scan)->chunkFreq[ii][jj][14] = 230.538e9 + sign*5123.0e6;
		    (*scan)->chunkFreq[ii][jj][15] = 230.538e9 + sign*5205.0e6;
		    (*scan)->chunkFreq[ii][jj][16] = 230.538e9 + sign*5287.0e6;
		    (*scan)->chunkFreq[ii][jj][17] = 230.538e9 + sign*5369.0e6;
		    (*scan)->chunkFreq[ii][jj][18] = 230.538e9 + sign*5451.0e6;
		    (*scan)->chunkFreq[ii][jj][19] = 230.538e9 + sign*5533.0e6;
		    (*scan)->chunkFreq[ii][jj][20] = 230.538e9 + sign*5615.0e6;
		    (*scan)->chunkFreq[ii][jj][21] = 230.538e9 + sign*5697.0e6;
		    (*scan)->chunkFreq[ii][jj][22] = 230.538e9 + sign*5779.0e6;
		    (*scan)->chunkFreq[ii][jj][23] = 230.538e9 + sign*5861.0e6;
		    (*scan)->chunkFreq[ii][jj][24] = 230.538e9 + sign*5943.0e6;
		  }
		  (*scan)->chunkFreq[ii][jj][49] = 230.538e9 + sign*9.0e9;
		  (*scan)->chunkFreq[ii][jj][50] = 230.538e9 + sign*11.0e9;
		}
	      }
	    }
	    alpha =  (*scan)->chunkFreq[rx][sb][uChunk] /
	      (*scan)->header.loData.restFrequency[effectiveRx];
	    alpha *= alpha;
	    beta = (1.0 - alpha)/(1.0 + alpha);
	    (*scan)->chunkVelo[rx][sb][uChunk] = 0.0;
	  }
	}
      /* Now do the SWARM chunks */
      for (chunk = 0; chunk < MAX_SWARM_CHUNK; chunk++) {
	if (sb == 0)
	  (*scan)->sWARMFreq[sb][chunk] = sWARMCenterFrequency/1.0e9 - SWARM_IF_MIDPOINT - chunk*SWARM_CHUNK_WIDTH;
	else
	  (*scan)->sWARMFreq[sb][chunk] = sWARMCenterFrequency/1.0e9 + SWARM_IF_MIDPOINT + chunk*SWARM_CHUNK_WIDTH;
	alpha = 1.0e9*((*scan)->sWARMFreq[rx][chunk])/fRest;
	alpha *= alpha;
	beta = (1.0 - alpha)/(1.0 + alpha);
	(*scan)->sWARMVelo[rx][chunk] = (SPEED_OF_LIGHT*beta - vRadial - vCatalog)/1.0e3;
      }
    } /* for (sb ... */
  }
} /* End of calculateChunkFrequencies */

/*

  M A K E  P A D  L I S T

  makePadList determines which pad each antenna is on.   This info
  probably should be passed by statusServer, instead of being calculated
  here.   I'll fix it later...
*/
void makePadList(pendingScan **scan)
{
  int ant, pad;
  double deltaX, deltaY, deltaZ, distance, minDistance;

  minDistance = 1.0e38;
  for (ant = 1; ant < MAX_ANT+1; ant++)
    for (pad = 1; pad <= MAX_PAD; pad++) {
      deltaX = (*scan)->header.DDSdata.x[ant] - nominalPadX[pad];
      deltaY = (*scan)->header.DDSdata.y[ant] - nominalPadY[pad];
      deltaZ = (*scan)->header.DDSdata.z[ant] - nominalPadZ[pad];
      distance = sqrt(deltaX*deltaX + deltaY*deltaY + deltaZ*deltaZ);
      if (distance < minDistance)
	minDistance = distance;
      if (distance < 1.0) {
	(*scan)->padList[ant] = pad;
	break;
      } else if (pad == MAX_PAD) {
	(*scan)->padList[ant] = 0;
      }
    }
} /* End of makePadList */

/*

  G E T  D S M  I N F O

  getDSMInfo pulls the data from DSM which needs to be associated with
  a particular scan.
*/
void getDSMInfo(pendingScan **scan)
{
  /* int ds; */
  int i;
  /* time_t timestamp; */
  
  /*
  ds = dsm_read("hal9000", "DSM_BDA_IF_SEP_D",
		(char *)&bDAIFSep, &timestamp);
  if (ds != DSM_SUCCESS) {
    dsm_error_message(ds, "dsm_read (1)");
    perror("getDSMInfo: dsm read of DSM_BDA_IF_SEP_D");
  }
  */
  bDAIFSep = 2.0e9;
  if (isnan(bDAIFSep) || (bDAIFSep < BWD_MIN_IF_SEPARATION) || (bDAIFSep > BWD_MAX_IF_SEPARATION)) {
    fprintf(stderr,"Got an illegal bDAIFSep value (%f) - will set to 2.0\n", bDAIFSep);
    bDAIFSep = 2.0e9;
  }
  dprintf("New bDAIFSep value: %f\n", bDAIFSep);
  /*
  ds = dsm_read("hal9000", "DSM_AS_POLAR_MODE_S",
		(char *)&((*scan)->dSMStuff.polarMode), &timestamp);
  if (ds != DSM_SUCCESS) {
    dsm_error_message(ds, "dsm_read (2)");
    perror("getDSMInfo: dsm read of DSM_AS_POLAR_MODE_S");
  }
  */
  (*scan)->dSMStuff.polarMode = FALSE;
  /*
  ds = dsm_read("hal9000", "DSM_AS_POINTING_MODE_S",
		(char *)&((*scan)->dSMStuff.pointingMode), &timestamp);
  if (ds != DSM_SUCCESS) {
    dsm_error_message(ds, "dsm_read (3)");
    perror("getDSMInfo: dsm read of DSM_AS_POINTING_MODE_S");
  }
  */
  (*scan)->dSMStuff.pointingMode = FALSE;
  /*
  ds = dsm_read("hal9000", "DSM_AS_POLAR_V11_C1",
		(char *)&((*scan)->dSMStuff.polarStates), &timestamp);
  if (ds != DSM_SUCCESS) {
    dsm_error_message(ds, "dsm_read (4)");
    perror("getDSMInfo: dsm read of DSM_AS_POLAR_V11_C1");
  }
  */
  for (i = 0; i < 11; i++)
    (*scan)->dSMStuff.polarStates[i] = 0;
} /* End of getDSMInfo */

/*                                                                                                                                                             H E A D E R                                                                                                                                              
  This function executes as a separate thread, it waits until a                                                                                              condition variable is signalled.   This signal indicates that                                                                                              the first visibility bundle for a new scan has arrived.   Once                                                                                             signalled, this function gets the header information from                                                                                                  hal9000, and stores it in the scan structure.                                                                                                            */
void *header(void *arg)
{
  int rCode, i, mirOK;
  requestCodes statusRequest;
  info *mirInfo = NULL;
  int nTimes = 0;
  double startTimeDouble, stopTimeDouble, thisTime;
  double timeSum = 0.0;
  double maxTime = -1.0e30;
  double minTime = 1.0e30;
  struct timespec startTime, stopTime;
  static int statusServerConnectionOK = FALSE;
  /* static CLIENT *statusServerCl, *setLOCl = NULL; */
  void *returnValue = NULL;

  printf("Thread HEADER starting\n");
  while (TRUE) {
    pthread_mutex_lock(&needHeaderMutex);
    while (!needHeader) {
      dprintf("header thread sleeping, awaiting a signal\n");
      rCode = pthread_cond_wait(&needHeaderCond, &needHeaderMutex);
      if (rCode) {
        fprintf(stderr,
                "header: Error %d returned by pthread_cond_wait\n",
                rCode);
        perror("pthread_cond_wait");
      }
    }
    printf("header thread re-awakened\n");
    clock_gettime(CLOCK_REALTIME, &startTime);
    startTimeDouble = ((double)startTime.tv_sec) + ((double)startTime.tv_nsec)*1.0e-9;
    /*                                                                                                                                                            Get the data from statusServer                                                                                                                          */
    mirOK = FALSE;
    i = 0;
    do {
      /* int dSMStatus; */
      /* static int badSetLOCall = FALSE; */
      /* time_t timeStamp; */
      char checkSourceName[35];
      /* struct timeval timeout; */

      /*
      if ((setLOCl == NULL) || badSetLOCall)
	setLOCl = clnt_create("hal9000", SETLOPROG, SETLOVERS, "tcp");
      */
      if (TRUE) {
	/* genericRequest request; */
	skyFrequencyInfo *info;

	/* get the information about sky frequencies needed for SWARM */
	/* info = getsky_1(&request, setLOCl); */
	if (FALSE) {
	  double skyFrequency;

	  if (info->sideband[0])
	    skyFrequency = info->frequency[0] + 5.0e9;
	    else
	    skyFrequency = info->frequency[0] - 5.0e9;
	  sWARMCenterFrequency = skyFrequency;
	  /* badSetLOCall = FALSE; */
	} else {
	  sWARMCenterFrequency = 2.3e11;
	  /* badSetLOCall = TRUE; */
	}
      }
      /* Try to connect to the statusServer - allow up to 8 retries before giving up */
      mirOK = TRUE;
      /*
      if ((!statusServerConnectionOK) || (statusServerCl == NULL)) {
        dprintf("Attempting to connect to statusServer on hal\n");
        statusServerCl = clnt_create("hal9000", STATUSSERVERPROG, STATUSSERVERVERS, "tcp");
      }
      */
      if (TRUE) {
        mirOK = FALSE;
        statusServerConnectionOK = FALSE;
	strcpy(globalSourceName, "LabAntennaSimulator");
        if (FALSE) {
          clnt_pcreateerror("hal9000");
          fprintf(stderr, "header: WARNING: Could not connect to statusServer.\n");
        }
      } else {
        statusServerConnectionOK = TRUE;
        /* timeout.tv_sec = 500.0; */
        /* timeout.tv_usec = 0.0; */
	/*
        if (clnt_control(statusServerCl, CLSET_TIMEOUT, (char *)&timeout) == FALSE) {
          fprintf(stderr, "header: Could not set hal9000 timeout to %d secs.\n", (int)timeout.tv_sec);
        }
	*/
        /*                                                                                                                                                            I must fetch the source name here, because the next operation will be to send                                                                              a request for header information to statusServer.   statusServer keeps track of the                                                                        scan count, so we may go to a new source immediately after that call, and then                                                                             the source name info might be wrong.                                                                                                             	*/
        /*
	dSMStatus = dsm_read("hal9000", "DSM_AS_SOURCE_C34", globalSourceName, &timeStamp);
        if (dSMStatus == DSM_SUCCESS)
          globalSourceName[25] = (char)0;
        else
	  dsm_error_message(dSMStatus, "DSM_AS_SOURCE_C34");
	*/
	strcpy(globalSourceName, "LabAntennaSimulator");
        statusRequest.utsec = headerScan->firstTime;
        statusRequest.interval = headerScan->intTime;
        dprintf("header:\tmaking call to statusServer (%f, %f)\n",
                statusRequest.utsec, statusRequest.interval);
        /* mirInfo = statusrequest_1(&statusRequest, statusServerCl); */
        if (FALSE) {
          struct rpc_err halerr;         /* RPC error structure */

          mirOK = statusServerConnectionOK = FALSE;
          fprintf(stderr, "header: WARNING: Opened connection to hal9000, but could not get mir data.\n");
          /* clnt_geterr(statusServerCl, &halerr); */
          fprintf(stderr, "header: RPC Error:  %s\n", clnt_sperrno(halerr.re_status));
	  /*
          if (halerr.re_status == RPC_TIMEDOUT) {
            if (clnt_control(statusServerCl, CLGET_TIMEOUT, (char *)&timeout) == FALSE) {
              fprintf(stderr, "header: Could not get hal9000 timeout\n");
            } else {
              fprintf(stderr, "header: timeout was set at %d secs.  %d\n",
                      (int)timeout.tv_sec, (int)timeout.tv_usec);
            }
          }
          auth_destroy(statusServerCl->cl_auth);
          clnt_destroy(statusServerCl);
	  */
        }
	/*
        dSMStatus = dsm_read("hal9000", "DSM_AS_SOURCE_C34", checkSourceName, &timeStamp);
        if (dSMStatus == DSM_SUCCESS)
          checkSourceName[25] = (char)0;
        else
          dsm_error_message(dSMStatus, "DSM_AS_SOURCE_C34 (check read)");
	*/
	strcpy(checkSourceName, "LabAntennaSimulator");
        if (mirInfo) {
          int i;

           for (i = 1; i < 9; i++) {
            if ((mirInfo->antavg[i].isvalid[0] == 1) && (mirInfo->antavg[i].isvalid[1] == 1024))
              mirInfo->antavg[i].isvalid[0] = 0;
           }
         } else
	  fprintf(stderr, "Skipping 1 && 1024 test, because of NULL pointer\n");
        if (strcmp(globalSourceName, checkSourceName)) {
          spoilScanFlag = TRUE;
        } else {
          static char lastSourceName[50];

          if (!strcmp(checkSourceName, lastSourceName))
            spoilScanFlag = FALSE;
          else {
            spoilScanFlag = TRUE;
          }
          strcpy(lastSourceName, checkSourceName);
        }
      }
    } while ((mirOK == FALSE) && (i++ < 1));
    /*
    if (mirOK && (!haveLOData)) {
      globalSkyFrequency[0] = mirInfo->loData.skyFrequency[0];
      globalSkyFrequency[1] = mirInfo->loData.skyFrequency[1];
      bcopy((char *)&(mirInfo->loData.frequencies), (char *)&(globalFrequencies),
            sizeof(globalFrequencies));
      haveLOData = TRUE;
    }
    */
    {
      int rx, sb, bl, ch;

      for (rx = 0; rx < 2; rx++)
	for (sb = 0; sb < 2; sb++)
	  for (bl = 0; bl < 6; bl++)
	    for (ch = 0; ch < 4; ch++)
	      globalFrequencies.receiver[rx].sideband[sb].block[bl].chunk[ch].centerfreq = 1.0e9;
    }
    printf("Done setting globalFrequencies\n");
    /* bcopy((char *)mirInfo, (char *)&(headerScan->header), sizeof(headerScan->header)); */
    {
      int ii;

      for (ii = 0; ii < 11; ii++) {
	headerScan->header.DDSdata.x[ii] = 1.0*ii;
	headerScan->header.DDSdata.y[ii] = 2.0*ii;
	headerScan->header.DDSdata.z[ii] = 3.0*ii;
      }
    }
    printf("header:\tDone with statusServer stuff\n");
    getDSMInfo(&headerScan);
    if (doDSMWrite && FALSE) {
      /* int dSMStatus; */

      /*                                                                                                                                                            Write scan number to DSM, with notify.   Some software, such as the                                                                                        polarization code (using waveplates) waits on this variable to know                                                                                        when the next integration may start, or a source change is allowable, etc.                                                                              */
      dprintf("Writing MIRNUM of %d\n", globalScanNumber);
      /*
      dSMStatus = dsm_write_notify("hal9000", "DSM_AS_SCAN_MIRNUM_L", (char *)&globalScanNumber);
      if (dSMStatus != DSM_SUCCESS) {
        dsm_error_message(dSMStatus, "dsm_write_notify");
        perror("header: dsm write scan num to Hal failed");
      }
      */
    }
    makePadList(&headerScan);
    calculateChunkFrequencies(&headerScan);
    /* Stuff the header information from statusServer into the scan. */
    pthread_mutex_lock(&scanMutex);
    headerScan->gotHeaderInfo = TRUE;
    scanCompleteCheck(headerScan);
    pthread_mutex_unlock(&scanMutex);
    needHeader = FALSE;
    pthread_mutex_unlock(&needHeaderMutex);
    clock_gettime(CLOCK_REALTIME, &stopTime);
    stopTimeDouble = ((double)stopTime.tv_sec) + ((double)stopTime.tv_nsec)*1.0e-9;
    thisTime = stopTimeDouble-startTimeDouble;
    if (thisTime > maxTime)
      maxTime = thisTime;
    if (thisTime < minTime)
      minTime = thisTime;
    timeSum += thisTime;
    nTimes++;
    if (thisTime > 0.25)
      printf("\t\t%%%% Header fetch took %f seconds (%f, %f, %f)\n",
             thisTime, timeSum/((double)nTimes), maxTime, minTime);
  } /* End of while (TRUE) */
  return(returnValue); /* Never Executed */
} /* End of header */

double calcJD(int year, int mon, int day, int hh, int mm, int ss)
{
  double Y, M, D, jD, A, B;

  Y = (double)year;
  M = (double)mon;
  D = (double)day;
  if (M < 3) {
    Y -= 1.0;
    M += 12.0;
  }
  A = (double)((int)(Y/100.0));
  B = 2.0 - A + (double)((int)(A/4.0));
  jD = (double)(((int)(365.25*(Y+4716.0))) + ((int)(30.6001*(M+1.0))) + D +B - 1524.5 + ((double)hh)/24.0 + ((double)mm)/1440.0 +((double)ss)/86400.0) ;
  return(jD);
}


/*

  F I X E D  C O D E S

  fixedCodes was swiped pretty much verbatim from Eric Keto's original
  version of this program.   The only changes were in syntax, to make
  the style more closely match the rest of this program, and the calls
  to "codeh_write" which Eric used to write the structure elements
  one by one were replaced by fwrite calls which write the entire
  structure in one call.   Eric wrote his structures one element
  at a time, because he did not use packed structures as this program
  does.

  Eric's original comment line:
  Writes the MIR ascii codes which do not depend on values in the data
*/
void fixedCodes(FILE *fpcodeh)
{
  
  int i;
  char intcode[3];
  struct codehDef codeh;
                                /* cocd: array configuration code, normally 4, A,B,C,D */
                                /* 1 configuration is enough for now */
  strcpy(codeh.v_name, "cocd"); /* label                      */
  codeh.icode     = 0;          /* index for a code word      */
  strcpy(codeh.code, "D");      /* the code word              */
  codeh.ncode     = 1;          /* no longer used             */
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* ut:  ut at start of integration, one for each integration, 
     done in main code, completed. */
  
  /* ref_time: reference time of integration, one or more, 
     done in main code, completed. */
  
  /* tq: tuning qualifier. Not sent by sma. Would be v01, v02 etc for different
     correlator configurations. There could be any number, but there is
     always at least one. Not done yet. */
  strcpy(codeh.v_name, "tq");
  codeh.icode     = 0;
  strcpy(codeh.code, "v01");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* vctype: velocity correction definition. Completed.*/
  strcpy(codeh.v_name, "vctype"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "vlsr");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "cz");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 2;
  strcpy(codeh.code, "vhel");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 3;
  strcpy(codeh.code, "pla");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* sb: sideband, lower or upper. Completed. */
  strcpy(codeh.v_name, "sb"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "l");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "u");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);

  /*pol: polarization, hh,vv,hv,vh, etc Completed. */
  if (fullPolarization) {
    strcpy(codeh.v_name, "pol"); 
    codeh.icode     = POL_STATE_UNKNOWN;
    strcpy(codeh.code, "Unknown");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_RR;
    strcpy(codeh.code, "RR");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_RL;
    strcpy(codeh.code, "RL");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_LR;
    strcpy(codeh.code, "LR");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_LL;
    strcpy(codeh.code, "LL");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_LH;
    strcpy(codeh.code, "LH");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_LV;
    strcpy(codeh.code, "LV");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_RH;
    strcpy(codeh.code, "RH");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_RV;
    strcpy(codeh.code, "RV");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_HR;
    strcpy(codeh.code, "HR");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_HL;
    strcpy(codeh.code, "HL");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_HH;
    strcpy(codeh.code, "HH");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_HV;
    strcpy(codeh.code, "HV");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_VR;
    strcpy(codeh.code, "VR");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_VL;
    strcpy(codeh.code, "VL");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_VH;
    strcpy(codeh.code, "VH");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = POL_STATE_VV;
    strcpy(codeh.code, "VV");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  } else {
    strcpy(codeh.v_name, "pol"); 
    codeh.icode     = 0;
    strcpy(codeh.code, "hh");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = 1;
    strcpy(codeh.code, "vv");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = 2;
    strcpy(codeh.code, "hv");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
    codeh.icode     = 3;
    strcpy(codeh.code, "vh");
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  }
    
  /* aq: amplitude qualifier, 3 possible 1 2 and ' '. Completed */
  strcpy(codeh.v_name, "aq"); 
  codeh.icode     = 0;
  strcpy(codeh.code, " ");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "1");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 2;
  strcpy(codeh.code, "2");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* bq: baseline qualifier, 2 possible b and ' '. Completed*/
  strcpy(codeh.v_name, "bq"); 
  codeh.icode     = 0;
  strcpy(codeh.code, " ");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "b");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* cq: coherence qualifier, 2 possible c and ' '. Completed */
  strcpy(codeh.v_name, "cq"); 
  codeh.icode     = 0;
  strcpy(codeh.code, " ");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "c");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* oq: offset qualifier, 2 possible o and ' '. Completed */
  strcpy(codeh.v_name, "oq"); 
  codeh.icode     = 0;
  strcpy(codeh.code, " ");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "o");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* rec: receiver, 3 possible 230, 345, 400, 690. Completed*/
  strcpy(codeh.v_name, "rec"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "230"); 
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "345"); 
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 2;
  strcpy(codeh.code, "400"); 
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 3;
  strcpy(codeh.code, "690"); 
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* ifc: if channel, 2 possible from receiver 1 or 2. Completed*/
  strcpy(codeh.v_name, "ifc"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "1");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "2");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* tel1: antenna number 1. icode will be antenna# . Completed */
  strcpy(codeh.v_name, "tel1"); 
  for (i = 0; i < MAX_ANTNUM+1; i++) {
    codeh.icode = i;
    sprintf(intcode, "%d", i);
    strcpy(codeh.code, intcode);
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  }
  
  /* tel2: antenna number 2. icode will be antenna# . Completed */
  strcpy(codeh.v_name, "tel2"); 
  for (i = 0; i < MAX_ANTNUM+1; i++) {
    codeh.icode = i;
    sprintf(intcode, "%d", i);
    strcpy(codeh.code, intcode);
    fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  }
  
  /* blcd: baseline code, use pad numbers. Done in main */
    
  /* gq: gain qualifier, 2 possible g and ' '. Completed */
  strcpy(codeh.v_name, "gq"); 
  codeh.icode     = 0;
  strcpy(codeh.code, " ");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "g");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* pq: passband qualifier, 2 possible p and ' '. Completed */
  strcpy(codeh.v_name, "pq"); 
  codeh.icode     = 0;
  strcpy(codeh.code, " ");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "p");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* band: band type, 49 possible s1-24 (or 48 in doubleBandwidth mode) and c1. 
     continuum done here, spectra done in main */
  strcpy(codeh.v_name, "band") ;
  codeh.icode = codeh.ncode = 0;
  strcpy(codeh.code, "c1");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);                                                  
  
  /* pstate: ???? . Can't find anyone at Caltech that remembers
     what this variable is used for. Completed */
  strcpy(codeh.v_name, "pstate"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "0");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* vtype: velocity correction definition. Completed */
  strcpy(codeh.v_name, "vtype"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "vlsr");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "cz");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 2;
  strcpy(codeh.code, "vhel");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 3;
  strcpy(codeh.code, "pla");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* taper: uniform or hanning smoothed u or h. Completed */
  strcpy(codeh.v_name, "taper"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "u");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "h");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* trans: line transition, anything, must be set in main. 
     Not done yet and may be a while. */
  strcpy(codeh.v_name, "trans"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "unspecified");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* source: name of source, must be set in main */
  
  /* pos: The name of an offset position. Must be set in main.
     Not done yet and may be a while. */
  strcpy(codeh.v_name, "pos"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "unspecified");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* offtype: The offset may be set as either az-el or ra-dec 2 possible */
  strcpy(codeh.v_name, "offtype"); 
  codeh.icode     = 0;
  strcpy(codeh.code, "ra-dec");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  codeh.icode     = 1;
  strcpy(codeh.code, "az-el");
  fwrite_unlocked(&codeh, sizeof(codehDef), 1, fpcodeh);
  
  /* ra: Right Ascension. must be set in main */
  /* dec: Declination. must be set in main */
  
} /* End of fixedCodes */

/*

  U T  T I M E  S T R

  uTTimeStr is another function pretty much stolen intact from Eric Keto's
  original version of this program.   Only cosmetic changes have been made.
*/
void uTTimeStr(int mon, int day, int yr, int hr, int min, double sec, char *ref_time)     
{
  char month[13][4];
  char st[5];
  int hr12;
  int intsec;

  strcpy(month[ 1], "Jan");
  strcpy(month[ 2], "Feb");
  strcpy(month[ 3], "Mar");
  strcpy(month[ 4], "Apr");
  strcpy(month[ 5], "May");
  strcpy(month[ 6], "Jun");
  strcpy(month[ 7], "Jul");
  strcpy(month[ 8], "Aug");
  strcpy(month[ 9], "Sep");
  strcpy(month[10], "Oct");
  strcpy(month[11], "Nov");
  strcpy(month[12], "Dec");
  
  strcpy(ref_time, month[mon]);
  strcat(ref_time, " ");
  sprintf(st, "%2d", day);
  strcat(ref_time, st);
  strcat(ref_time, " ");
  sprintf(st, "%4d", yr);
  strcat(ref_time, st);
  strcat(ref_time, " ");
  if (hr < 12)
    hr12 = hr;
  else
    hr12 = hr - 12;
  if (hr12 < 10) {
    strcat(ref_time, " ");
    sprintf(st, "%1d", hr12);
  } else
    sprintf(st, "%2d", hr12);
  strcat(ref_time, st);
  strcat(ref_time, ":");
  if (min < 10) {
    strcat(ref_time, "0");
    sprintf(st, "%1d", min);
  } else
    sprintf(st, "%2d", min);
  strcat(ref_time, st);
  strcat(ref_time, ":");
  
  intsec = sec;
  if (intsec < 10) {
    strcat(ref_time, "0");
    sprintf(st, "%1d", intsec);
  } else
    sprintf(st, "%2d", intsec);
  strcat(ref_time, st);
  
  strcat(ref_time, ".");
  intsec = (sec - intsec)*1000.0;
  if (intsec < 10) {
    strcat(ref_time, "00");
    sprintf(st, "%1d", intsec);
  } 
  if (intsec >= 10 && intsec < 100) {
    strcat(ref_time, "0");
    sprintf(st, "%2d", intsec);
  } 
  if (intsec >= 100 )
    sprintf(st, "%3d", intsec);
  
  strcat(ref_time, st);  
  
  if (hr < 12)
    strcat(ref_time, "AM");
  else
    strcat(ref_time, "PM");
} /* of uTTimeStr */

/*

  C A L D A T

  Calculates calendar day from Julian day
  Lifted from "Numerical Recipies in C"
*/
void caldat(julian, mm, id, iyyy)
     long julian;
     int *mm, *id, *iyyy;
{
  long ja, jalpha, jb, jc, jd, je;
  
  if (julian >= 2299161) {
    jalpha=((float) (julian-1867216)-0.25)/36524.25;
    ja=julian+1+jalpha-(long) (0.25*jalpha);
  } else
    ja=julian;
  jb=ja+1524;
  jc=6680.0+((float) (jb-2439870)-122.1)/365.25;
  jd=365*jc+(0.25*jc);
  je=(jb-jd)/30.6001;
  *id=jb-jd-(int) (30.6001*je);
  *mm=je-1;
  if (*mm > 12)
    *mm -= 12;
  *iyyy=jc-4715;
  if (*mm > 2)
    --(*iyyy);
  if (*iyyy <= 0)
    --(*iyyy);
} /* caldat */

/*

  R E F  T I M E  S T R

  This function was lifted from Eric's original version of this
  program, with only cosmetic changes.

  Original Keto Komment:
  Makes an ascii string of the day and time
*/
void refTimeStr(int mon, int day, int yr, char *ref_time)     
{
  char month[13][4];
  char st[5];
  strcpy(month[ 1], "Jan");
  strcpy(month[ 2], "Feb");
  strcpy(month[ 3], "Mar");
  strcpy(month[ 4], "Apr");
  strcpy(month[ 5], "May");
  strcpy(month[ 6], "Jun");
  strcpy(month[ 7], "Jul");
  strcpy(month[ 8], "Aug");
  strcpy(month[ 9], "Sep");
  strcpy(month[10], "Oct");
  strcpy(month[11], "Nov");
  strcpy(month[12], "Dec");
  
  strcpy(ref_time, month[mon]);
  strcat(ref_time, " ");
  sprintf(st, "%2d", day);
  strcat(ref_time, st);
  strcat(ref_time, ", ");
  sprintf(st, "%4d", yr);
  strcat(ref_time, st);
} /* of refTimeStr */

/*

  C O O R D  S T R

  Yet another function pilfered from Eric's original version of
  this program, with only cosmetic changes.

  Origian Keto Komment:
  Makes an ascii string of the RA or Dec
*/
void coordStr(double radec, char *ch_str, int isdec)
{
  int hr, min, sec, fsec;
  char intcode[3];
  
  if (isdec) {
    radec = radec * RADIANS_TO_DEGREES;
    if (radec < 0.0)
      strcpy(ch_str, "-");
    else
      strcpy(ch_str, "+");
  } else {
    radec = radec * RADIANS_TO_HOURS;
    strcpy(ch_str, "");
  }
  
  radec = fabs(radec);  
  hr = radec;
  min = (radec - hr)*60.0;
  sec = (radec - hr - min/60.0)*3600.0;
  if (isdec)
    fsec = (radec - hr - min/60.0 - sec/3600.0)*10.0*3600.0;
  else
    fsec = (radec - hr - min/60.0 - sec/3600.0)*100.0*3600.0;
  
  if (hr < 10) {
    strcat(ch_str, "0");
    sprintf(intcode, "%d", hr);
    strcat(ch_str, intcode);
  } else {
    sprintf(intcode, "%d", hr);
    strcat(ch_str, intcode);
  }
  
  strcat(ch_str, ":");
  
  if (min < 10) {
    strcat(ch_str, "0");
    sprintf(intcode, "%d", min);
    strcat(ch_str, intcode);
  } else {
    sprintf(intcode, "%d", min);
    strcat(ch_str, intcode);
  }
  
  strcat(ch_str, ":");
  
  if (sec < 10) {
    strcat(ch_str, "0");
    sprintf(intcode, "%d", sec);
    strcat(ch_str, intcode);
  } else {
    sprintf(intcode, "%d", sec);
    strcat(ch_str, intcode);
  }
  
  strcat(ch_str, ".");
  
  if (isdec) {
    sprintf(intcode, "%d", fsec);
    strcat(ch_str, intcode);
  } else {
    if (fsec < 10) {
      strcat(ch_str, "0");
      sprintf(intcode, "%d", fsec);
      strcat(ch_str, intcode);
    } else {
      sprintf(intcode, "%d", fsec);
      strcat(ch_str, intcode);
    }
  }
} /* end of coordStr */

/*

  W R I T E  E N G  D A T A

  writeEngData writes everything needed in the eng_read data file
  for a single antenna.
*/
void writeEngData(int ant, int pad, antDataDef data, FILE *file) {
  antEngDef antData;

  antData.antennaNumber            = ant;
  antData.padNumber                = pad;
  if (data.antennaStatus == IS_OFFLINE)
    antData.antennaStatus          = IS_OFFLINE;
  else
    antData.antennaStatus          = data.isvalid[0];
  antData.trackStatus              = data.trackStatus;
  antData.commStatus               = 1;
  antData.inhid                    = globalScanNumber;
  antData.ints                     = globalScanNumber;
  antData.dhrs                     = data.utc;
  antData.ha                       = data.hour_angle;
  antData.lst                      = data.lst;
  antData.pmdaz                    = data.pmdaz;
  antData.pmdel                    = data.pmdel;
  antData.tiltx                    = data.tiltx;
  antData.tilty                    = data.tilty;
  antData.actual_az                = data.actual_az;
  antData.actual_el                = data.actual_el;
  antData.azoff                    = data.azoff;
  antData.eloff                    = data.eloff;
  antData.az_tracking_error        = data.az_tracking_error;
  antData.el_tracking_error        = data.el_tracking_error;
  antData.refraction               = data.refraction;
  antData.chopper_x                = data.chopper_x;
  antData.chopper_y                = data.chopper_y;
  antData.chopper_z                = data.chopper_z;
  antData.chopper_angle            = data.chopper_angle;
  antData.tsys                     = data.tsys;
  antData.tsys_rx2                 = data.tsys_rx2;
  antData.ambient_load_temperature = data.ambient_load_temperature;
  fwrite_unlocked(&antData, sizeof(antEngDef), 1, file);
} /* End of writeEngData */

/*

  S C H  W R I T E

  schWrite has been stolen nearly verbatim from Eric's original
  version of this program.   In Eric's program, every data
  file structure was written out element-by-element as this
  function does with the schDef structure.   But this is
  the only variable-length structure (because of the packed data)
  so it is the only one I need to write out element-by-element.
  There aren't many elements so it is no big deal.
*/
int schWrite(schDef *sch, FILE *fpsch)
{
  int size;
  int nbytes;   /* counts number of bytes written */
  
  nbytes = 0;
  size = sch->nbyt;

  sch->inhid     = sch->inhid;
  sch->nbyt      = sch->nbyt;

  fwrite_unlocked(&(sch->inhid), sizeof(sch->inhid), 1, fpsch);
  nbytes += sizeof(sch->inhid);
  fwrite_unlocked(&(sch->nbyt), sizeof(sch->nbyt), 1, fpsch);
  nbytes += sizeof(sch->nbyt);
  dprintf("Writing %d bytes of packed data\n", size);
  fwrite_unlocked(sch->packdata, size, 1, fpsch);
  nbytes += size;

  return nbytes;  
} /* end of schWrite */

/*

  P A C K  D A T A

  packData puts one spectrum (or continuum channel) into the one
  dimensional array of short integers that holds the scaled
  data (as complex numbers).
*/
int packData(int nChan, float intTime, float *real, float *imag, short *slot)
{
  short scaleExp;
  short	visRealS; /* real part of complex visibility scaled to short	   */
  short	visImagS; /* imaginary part of complex visibility scaled to short */
  int i, status;
  float delta, scale;
  float dataMax = -1.0e38;
  float dataMin = 1.0e38;

  status = -1;
  /* Find range of values in this set */
  for (i = 0; i < nChan; i++) {
    if (real[i] < dataMin)
      dataMin = real[i];
    if (real[i] > dataMax)
      dataMax = real[i];
    if (imag[i] < dataMin)
      dataMin = imag[i];
    if (imag[i] > dataMax)
      dataMax = imag[i];
  }
  if ((dataMax != 0.0) || (dataMin != 0.0))
    status = 0;
  if (fabs(dataMin) > dataMax)
    dataMax = fabs(dataMin);
  delta = dataMax/32767.0;
  scaleExp = log(delta)/log(2.0);
  if (scaleExp > 0)
    scaleExp++;
  scale = pow(2.0, (float)scaleExp);

  slot[0] = scaleExp;
  for (i = 0; i < nChan; i++) {
    visRealS = real[i]/scale;
    visImagS = imag[i]/scale;
    slot[1 + 2*i] = visRealS;
    slot[2 + 2*i] = visImagS;
  }
  return(status);
} /* End of packData */

/*
  C A L C  L A M B D A

  Return the wavelength for a particular frequency if the frequency
  passes sanity checks.
*/
double calcLambda(double freq)
{
  static int sentOneBad = FALSE;

  if ((freq > 1.0e11) && (freq < 1.0e12) && (!isnan(freq))) {
    sentOneBad = FALSE;
    return(SPEED_OF_LIGHT / freq);
  } else {
    if (!sentOneBad) {
      fprintf(stderr, "calcLambda(%f) is not good - will return 1.0\n", freq);
      sentOneBad = TRUE;
    }
    return(1.0);
  }
} /* End of calcLambda */

/*
  P O L A R  S T A T E  C O D E

  Return the integer code corresponding to a baseline's polarization
 */
int polarStateCode(int pol, dSMInfo info, int ant1, int ant2)
{
  int returnVal;

  if (fullPolarization) {
    /*
      Here we figure out what the polarization state of a particular scan is, based on
      the pol variable, which encodes which pair of receivers (345 or 400) produced
      the data, and what the position of the waveplates is on the two receivers.
    */
    switch (pol) {
    case 0:
      if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_RR;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_RL;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_LR;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_LL;

      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_RV;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_RH;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_LV;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_LH;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_VR;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_VL;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_VV;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_VH;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_HR;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_HL;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_HV;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_HH;
      else
	returnVal = POL_STATE_UNKNOWN;
      break;
    case 1:
      if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_LL;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_LR;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_RL;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_RR;

      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_LH;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_LV;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_RH;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_RV;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_HL;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_HR;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_HV;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_HV;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_VL;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_VR;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_VH;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_VV;
      else
	returnVal = POL_STATE_UNKNOWN;
      break;
    case 2:
      if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_RL;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_RR;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_LL;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_LR;

      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_RH;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_RV;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_LH;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_LV;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_VL;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_VR;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_VV;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_VV;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_HL;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_HR;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_HH;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_HV;
      else
	returnVal = POL_STATE_UNKNOWN;
      break;
    case 3:
      if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_LR;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_LL;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_RR;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_RL;

      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_LV;
      else if ((info.polarStates[ant1] == 'L') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_LH;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_RV;
      else if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_RH;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_HR;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_HL;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_HV;
      else if ((info.polarStates[ant1] == 'H') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_HH;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'L'))
	returnVal = POL_STATE_VR;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal = POL_STATE_VL;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'H'))
	returnVal = POL_STATE_VV;
      else if ((info.polarStates[ant1] == 'V') &&
	  (info.polarStates[ant2] == 'V'))
	returnVal = POL_STATE_VH;
      else
	returnVal = POL_STATE_UNKNOWN;
      break;
    default:
      returnVal = POL_STATE_UNKNOWN;
    }
  } else { /* Not in full polarization mode */
    if (info.polarMode != 1)
      returnVal = 0;   /*    polarization int code     */
    else {
      if ((info.polarStates[ant1] == 'R') &&
	  (info.polarStates[ant2] == 'R'))
	returnVal      = POL_STATE_RR;
      else if ((info.polarStates[ant1] == 'R') &&
	       (info.polarStates[ant2] == 'L'))
	returnVal      = POL_STATE_RL;
      else if ((info.polarStates[ant1] == 'R') &&
	       (info.polarStates[ant2] == 'V'))
	returnVal      = 13;
      else if ((info.polarStates[ant1] == 'R') &&
	       (info.polarStates[ant2] == 'H'))
	returnVal      = 14;
      else if ((info.polarStates[ant1] == 'L') &&
	       (info.polarStates[ant2] == 'R'))
	returnVal      = POL_STATE_LR;
      else if ((info.polarStates[ant1] == 'L') &&
	       (info.polarStates[ant2] == 'L'))
	returnVal      = POL_STATE_LL;
      else if ((info.polarStates[ant1] == 'L') &&
	       (info.polarStates[ant2] == 'V'))
	returnVal      = 15;
      else if ((info.polarStates[ant1] == 'L') &&
	       (info.polarStates[ant2] == 'H'))
	returnVal      = 16;
      else if ((info.polarStates[ant1] == 'V') &&
	       (info.polarStates[ant2] == 'R'))
	returnVal      = 5;
      else if ((info.polarStates[ant1] == 'V') &&
	       (info.polarStates[ant2] == 'L'))
	returnVal      = 6;
      else if ((info.polarStates[ant1] == 'V') &&
	       (info.polarStates[ant2] == 'V'))
	returnVal      = 7;
      else if ((info.polarStates[ant1] == 'V') &&
	       (info.polarStates[ant2] == 'H'))
	returnVal      = 8;
      else if ((info.polarStates[ant1] == 'H') &&
	       (info.polarStates[ant2] == 'R'))
	returnVal      = 9;
      else if ((info.polarStates[ant1] == 'H') &&
	       (info.polarStates[ant2] == 'L'))
	returnVal      = 10;
      else if ((info.polarStates[ant1] == 'H') &&
	       (info.polarStates[ant2] == 'V'))
	returnVal      = 11;
      else if ((info.polarStates[ant1] == 'H') &&
	       (info.polarStates[ant2] == 'H'))
	returnVal      = 12;
      else
	returnVal      = 0;
    }
  }
  return(returnVal);
} /* End of polarStateCode */

void *copier(void *arg) {
  /* int dSMStatus; */
  /* long scansRemaining, lastScansRemaining = 0; */
  /* time_t timestamp; */

  while (TRUE) {
    /*
    dSMStatus = dsm_read("hal9000", "DSM_AS_SCANS_REMAINING_L", (char *)&scansRemaining,
                         &timestamp);
    if (dSMStatus != DSM_SUCCESS)
      dsm_error_message(dSMStatus, "dsm_read (copier)");
    if (scansRemaining != lastScansRemaining) {
      dSMStatus = dsm_write("obscon", "DSM_AS_SCANS_REMAINING_L", (char *)&scansRemaining);
      if (dSMStatus != DSM_SUCCESS)
        dsm_error_message(dSMStatus, "dsm_write_notify (copier)");
      lastScansRemaining = scansRemaining;
    }
    */
    sleep(1);
  }
}

/*

   W R I T E  A U T O  D A T A

   This function writes out the autocorrelation scans from the SWARM correlator.

*/

void writeAutoData(int scan) {
  static int autoFileOpen = FALSE;
  static FILE *autoFile;
  sWARMAutoRec *ptr, *tptr;

  if (!autoFileOpen) {
    char autoFileName[1000];
    
    sprintf(autoFileName, "%s/autoCorrelations", pathName);
    autoFile = fopen(autoFileName, "w");
    if (autoFile == NULL) {
      perror("opening autoFile");
      return;
    }
    autoFileOpen = TRUE;
  }
  pthread_mutex_lock(&autoMutex);
  ptr = sWARMAutoRoot;
  printf("Looking for autocorrelations to store...\n");
  while (ptr != NULL) {
    ptr->autoData.scan = scan;
    printf("Writing autocorrelation scan %d for ant %d\n", ptr->autoData.scan, ptr->autoData.antenna);
    fwrite_unlocked(&(ptr->autoData), sizeof(autoCorrDef), 1, autoFile);
    tptr = ptr;
    ptr = ptr->next;
    free(tptr);
  }
  sWARMAutoRoot = NULL;
  pthread_mutex_unlock(&autoMutex);
  fflush_unlocked(autoFile);
} /* End of writeAutoData */

/*
  
  W R I T E R
  
  This function executes as a separate thread, it waits until a
  condition variable is signalled.   This signal indicates that
  a scan is ready to be written to the data file.   If needed,
  the directory and new data files are created.   The scan is
  written, and once written the scan structure is removed
  from the linked list and its storage space is freed.
*/
void *writer(void *arg)
{
  /* short shortZero = 0; */
  int rCode, i, j, lowestAntennaNumber, lowestCrateNumber, nCrates;
  int tsysByteOffset = 0;
  double gunnLO[MAX_RX];
  double cabinLO[2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
  double corrLO1[2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
  double corrLO2[2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
  int antTsysByteOffset[MAX_ANT+1] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  int numberOfPolarizations = 0;
  int thisScanWasGood;
  int plotFileOpen = FALSE;
  int weFileOpen = FALSE;
  int tsysFileOpen = FALSE;
  int modeFileWritten = FALSE;
  int antFileWritten = FALSE;
  int pIFileWritten = FALSE;
  int codeVersionFileWritten = TRUE;
  int baselineFileOpen = FALSE;
  int codesFileOpen = FALSE;
  int engFileOpen = FALSE;
  int inFileOpen = FALSE; /* despite its name, we only do output with it */
  int spFileOpen = FALSE;
  int schFileOpen = FALSE;
  int ind1, ind2, ind3, ind4, i1Stop, i2Stop, i3Stop, i4Stop;
  unsigned int polarInt;
  int numberOfBaselines, numberOfSidebands, numberOfReceivers;
  int pCNPoints[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  int pCFreqNPoints[MAX_RX+1][MAX_SB][MAX_POLARIZATION];
  int inhid = 0, sphid = 0, blhid = 0; /* Keto Komment: unique identifiers for each individual  */
                                       /* integration, baseline & spectrum. inhid is set to the */
		                       /* scan number supplied by the crate controller          */
  int reverseBslnIndx[MAX_ANT+1][MAX_ANT+1][MAX_SB];
  int nBands[MAX_RX+1], band;
  int setStart, setStop, setInc;
  int nChannels[MAX_RX+1][2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
  int obsType, ant1, ant2, tempScanNumber;
  int thisSourceId;
  int thisVRadId = -1;
  int specOffset[MAX_RX][MAX_SIDEBAND][MAX_POLARIZATION][MAX_BASELINE][2*MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1];
  /* int pathNameChanged = FALSE; */
  int ant1N = 0, ant2N = 0, rxN = 0;
  double pCFreqSum[MAX_RX+1][MAX_SB][MAX_POLARIZATION];
  double pCVeloSum[MAX_RX+1][MAX_SB][MAX_POLARIZATION];
  float pCAmpSum[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  float nPCCohPoints[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  float pCRealSum[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  float pCImagSum[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  float pCAmp[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  double pCFreq[MAX_RX+1][MAX_SB][MAX_POLARIZATION];
  float pCVelo[MAX_RX+1][MAX_SB][MAX_POLARIZATION];
  float pCPhase[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  float pCCoh[MAX_RX+1][MAX_ANT+1][MAX_ANT+1][MAX_SB][MAX_POLARIZATION];
  int nCratesReportingTime;
  double averageTime, vRadial;
  char antOnline[11];
  char utstring[30];      /* storage for UT time in ascii */
  char sourceList[MAX_SOURCES][34]; /* List of source names already used */
  pendingScan scanCopy;
  codehDef codeh;
  inhDef inh;
  sphDef sph;
  schDef sch;
  wehDef weh;
  tsysRecordDef tsysh;
  /*
  dsm_structure visibilityInfo;
  */
  int nTimes = 0;
  /* time_t timestamp; */
  double startTimeDouble, stopTimeDouble, timeSum = 0.0, thisTime;
  double maxTime = -1.0e30;
  double minTime = 1.0e30;
  struct timespec startTime, stopTime;
  FILE *plotFile[MAX_RX], *baselineFile = NULL, *codesFile = NULL , *engFile = NULL,
    *inFile = NULL, *spFile = NULL, *schFile = NULL, *antFile, *pIFile, *weFile = NULL;
  FILE *modeFile, *tsysFile = NULL;

  printf("Thread WRITER starting\n");
  { /* Assemble the LO frequency information */
    short gunnN[2], gunnPLLN[2], block, chunk;
    double yIGFrequencies[2], blockLOs[7], chunkLOs[5];
    /* CLIENT *blockscl; */
    /* blksCommand dummy; */
    blksInfo *fromBlocks1 = NULL, *fromBlocks2 = NULL;

    /*
    i = dsm_read("hal9000", "DSM_AS_IFLO_MRG_V2_D", &yIGFrequencies, &timestamp);
    if (i != DSM_SUCCESS) {
      dsm_error_message(i, "dsm_read (DSM_AS_IFLO_MRG_V2_D)");
      yIGFrequencies[0] = yIGFrequencies[1] = 100.0e9;
    }
    */
    yIGFrequencies[0] = yIGFrequencies[1] = 100.0e9;

    /*
    i = dsm_read("hal9000", "DSM_AS_IFLO_GUNN_N_V2_S", &gunnN, &timestamp);
    if (i != DSM_SUCCESS) {
      dsm_error_message(i, "dsm_read (DSM_AS_IFLO_GUNN_N_V2_S)");
      gunnN[0] = gunnN[1] = 3;
    }
    */
    gunnN[0] = gunnN[1] = 3;

    /*
    i = dsm_read("hal9000", "DSM_AS_IFLO_GPLL_N_V2_S", &gunnPLLN, &timestamp);
    if (i != DSM_SUCCESS) {
      dsm_error_message(i, "dsm_read (DSM_AS_IFLO_GPLL_N_V2_S)");
      gunnPLLN[0] = gunnPLLN[1] = 14;
    }
    */
    gunnPLLN[0] = gunnPLLN[1] = 14;

    for (i = 0; i < 2; i++) {
      gunnLO[i] = ((double)gunnN[i])*(yIGFrequencies[i]*((double)gunnPLLN[i])+109.0e6);
    }
    i = FALSE;
    /*
    if (!(blockscl = clnt_create("blocks1.rt.sma", BLKSPROG, BLKSVERS, "tcp"))) {
      clnt_pcreateerror("blocks1");
    } else {
      fromBlocks1 = blksinquiry_1(&dummy, blockscl);
      if (fromBlocks1 != NULL)
	i = TRUE;
      clnt_destroy(blockscl);
    }
    */
    if (TRUE) {
      blockLOs[1] = 3180.0e6;
      blockLOs[2] = 3508.0e6;
      blockLOs[3] = 3836.0e6;
      blockLOs[4] = 6164.0e6;
      blockLOs[5] = 6492.0e6;
      blockLOs[6] = 6820.0e6;
    } else {
      for (i = 1; i <= 6; i++)
	blockLOs[i] = fromBlocks1->lO1Frequency[i-1];
    }
    i = FALSE;
    /*
    if (!(blockscl = clnt_create("blocks2.rt.sma", BLKSPROG, BLKSVERS, "tcp"))) {
      clnt_pcreateerror("blocks2");
    } else {
      fromBlocks2 = blksinquiry_1(&dummy, blockscl);
      if (fromBlocks2 != NULL)
	i = TRUE;
	clnt_destroy(blockscl);
	}
    */
    if (TRUE) {
      chunkLOs[1] =  724.0e6;
      chunkLOs[2] =  806.0e6;
      chunkLOs[3] = 1194.0e6;
      chunkLOs[4] = 1276.0e6;
    } else {
      for (i = 1; i <= 4; i++)
	chunkLOs[i] = fromBlocks2->lO2Frequency[i-1];
    }
    for (i = 1; i <= 48; i++) {
      block = (((i-1)/4)%6)+1;
      chunk = ((i-1)%4)+1;
      if (block > 3)
	chunk = 5 - chunk;
      if (((i < 25) || (i > 48)) || (!doubleBandwidth))
	cabinLO[i] = 0.0;
      else
	cabinLO[i] = 2.0e9;
      if (chunk < 3)
	corrLO2[i] = chunkLOs[chunk]+104.0e6;
      else
	corrLO2[i] = chunkLOs[chunk]-208.0e6;
      corrLO1[i] = blockLOs[block];
      if (block > 3)
	corrLO2[i] = -(corrLO2[i]+104.0e6);
    }
    cabinLO[49] = cabinLO[50] = cabinLO[1];
    corrLO1[49] = corrLO1[50] = corrLO1[1];
    corrLO2[49] = corrLO2[50] = corrLO2[1];
  }
  if (!chunkSNameInitialized) {
    for (i = 0; i < MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1; i++)
      for (j = 0; j < MAX_RX+1; j++)
	chunkSName[j][i] = UNINITIALIZED;
    chunkSNameInitialized = TRUE;
  }
  if (fullPolarization)
    numberOfPolarizations = 4;
  else
    numberOfPolarizations = 1;
  /*
  dSMStatus = dsm_structure_init(&visibilityInfo, "LAST_SCAN_VISIBILITES_X");
  if (dSMStatus != DSM_SUCCESS)
    dsm_error_message(dSMStatus, "dsm_structure_init()");
  */
  for (i = 0; i < MAX_RX+1; i++)
    for (j = 0; j < MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1; j++)
      nChannels[i][j] = 0;
  initializeArrays();

  /*
    Loop forever, waiting for scans to become writable (all crates
     have set data within the time window, and header data is available).
  */
  while (TRUE) {
    pthread_mutex_lock(&writeScanMutex);
    while (!needWrite) {
      printf("writer thread sleeping, awaiting a signal\n");
      rCode = pthread_cond_wait(&writeScanCond, &writeScanMutex);
      if (rCode) {
	fprintf(stderr,
		"writer: Error %d returned by pthread_cond_wait\n",
		rCode);
	perror("pthread_cond_wait");
      }
      printf("writer thread re-awakened ");
    }
    thisScanWasGood = TRUE;
    clock_gettime(CLOCK_REALTIME, &startTime);
    startTimeDouble = ((double)startTime.tv_sec) + ((double)startTime.tv_nsec)*1.0e-9;
    /* The following dsm_write enables operator messages for bad scans */
    /*
    dSMStatus = dsm_write("hal9000", "DSM_AS_CORRELATOR_RESTARTING_S", (char *)&shortZero);
    if (dSMStatus != DSM_SUCCESS) {
      dsm_error_message(dSMStatus, "dsm_write");
      perror("writer: dsm write of file name to Hal failed");
    }
    */
    sendOperatorMessages = TRUE;
    printf("and proceeding to write scan %d\n", globalScanNumber);
    /*
      Make a temporary copy of the scan for processing, so that
      we won't hold the SERVER thread waiting for the scanMutex.
    */
    pthread_mutex_lock(&scanMutex);
    bcopy((char *)writableScan, (char *)(&scanCopy), sizeof(scanCopy));
    for (i = 0; i <= MAX_CRATE; i++)
      if (writableScan->received[i])
	bundleCopy(writableScan->data[i], &(scanCopy.data[i]), FALSE,
		   &(writableScan->hiRes[i]), &(writableScan->nDaisyChained[i]),
		   &(writableScan->nInDaisyChain[i]));
    deleteScan(writableScan, TRUE);
    pthread_mutex_unlock(&scanMutex);
    /*
      Sum the Hi-Res mode partial chunks!

      When a Hi-Res mode is in use, more than one crate processes the data
      from a single chunk.   The crates send dataCatcher indendant full chunk
      spectra, which must be averaged together to produce a full signal/noise
      final chunk.   Here's where that is done.
    */
    for (i = 0; i <= MAX_CRATE; i++)
      if (scanCopy.received[i])
	if (scanCopy.hiRes[i] && (scanCopy.nInDaisyChain[i] == 1)) {
	  int jj, set, sb, kk;

	  for (jj = i+1; jj < i+scanCopy.nDaisyChained[i]; jj++) {
	    for (set = 0; set < scanCopy.data[jj]->set.set_len; set++) {
	      if (scanCopy.data[i]->set.set_val[set].chunkNumber == 1) {
		for (sb = 0; sb < scanCopy.data[i]->set.set_val[set].real.real_len; sb++) {
		  for (kk = 0; kk < scanCopy.data[i]->set.set_val[set].real.real_val[sb].channel.channel_len; kk++) {
		    scanCopy.data[i]->set.set_val[set].real.real_val[sb].channel.channel_val[kk] +=
		      scanCopy.data[jj]->set.set_val[set].real.real_val[sb].channel.channel_val[kk];
		    scanCopy.data[i]->set.set_val[set].imag.imag_val[sb].channel.channel_val[kk] +=
		      scanCopy.data[jj]->set.set_val[set].imag.imag_val[sb].channel.channel_val[kk];
		  }
		}
	      }
	    }
	  }

	  /* Normalize the summed chunk by the number of crates in the Daisy-chain */
	  if (scanCopy.nDaisyChained[i] > 0) {
	    float normalizer;
	    
	    normalizer = 1.0 / scanCopy.nDaisyChained[i];
	    for (set = 0; set < scanCopy.data[i]->set.set_len; set++)
	      for (sb = 0; sb < scanCopy.data[i]->set.set_val[set].real.real_len; sb++)
		for (kk = 0; kk < scanCopy.data[i]->set.set_val[set].real.real_val[sb].channel.channel_len; kk++) {
		  scanCopy.data[i]->set.set_val[set].real.real_val[sb].channel.channel_val[kk] *= normalizer;
		  scanCopy.data[i]->set.set_val[set].imag.imag_val[sb].channel.channel_val[kk] *= normalizer;
		}
	  }
	} /* if (scanCopy.hiRes[i] && (scanCopy.nInDaisyChain[i] == 1)) */
    /* End of HiRes summing loop */

    /* Find lowest antenna active for this scan */
    lowestAntennaNumber = getAntennaList(&antennaInArray[0]);
    antennaInArrayInitialized = TRUE;
    printf("...---... lowestAntennaNumber = %d\n", lowestAntennaNumber);
    if (lowestAntennaNumber > 0) {
      short startChannel, endChannel;
      int rx, set, sb, pol, chunk, crate, bl, flag, ira, idec;
      int effRx = 0, mon, day, yr, hr, min, sec, source;
      long jD;
      double lambda = 1.0;
      double rar, decr, az, el, hAMidpoint;
      char currentSource[24];

      /*
	Now do the actual writing of the data
      */
      dprintf("writer:\tlowest active antenna is %d\n", lowestAntennaNumber);
      if (needNewDataFile) {
	int dirCode;
	/* int ii, jj, kk, ds; */
	/* double dSMChunkFreqs[2][2][24], dSMChunkVelos[2][2][24]; */
	char directory[50], fileName[100];
	time_t now;
	struct tm *nowValues;
	/* dsm_structure plotInfo; */

	/* Write out some frequency information for corrPlotter */
	/* dSMStatus = dsm_structure_init(&plotInfo, "PLOT_FREQ_INFO_X"); */
	/*
	for (ii = 0; ii < 2; ii++)
	  for (jj = 0; jj < 2; jj++)
	    for (kk = 0; kk < 24; kk++) {
	      dSMChunkFreqs[ii][jj][kk] = scanCopy.chunkFreq[ii][jj][kk+1];
	      dSMChunkVelos[ii][jj][kk] = scanCopy.header.loData.vRadial = 0.0;
	    }
	*/
	/*
	dSMStatus = dsm_structure_set_element(&plotInfo, "CHUNK_FREQUENCIES_V2_V2_V24_D",
					      (char *)dSMChunkFreqs);
	if (dSMStatus != DSM_SUCCESS)
	  dsm_error_message(dSMStatus, "CHUNK_FREQUENCIES_V2_V2_V24_D");
	dSMStatus = dsm_structure_set_element(&plotInfo, "CHUNK_VRADIAL_V2_V2_V24_D",
					      (char *)dSMChunkVelos);
	if (dSMStatus != DSM_SUCCESS)
	  dsm_error_message(dSMStatus, "CHUNK_VRADIAL_V2_V2_V24_D");
	dSMStatus = dsm_write("obscon", "PLOT_FREQ_INFO_X", &plotInfo);
	if (dSMStatus != DSM_SUCCESS)
	  dsm_error_message(dSMStatus, "dsm_write()");
	*/
	/*
	  Create the directory
	*/
	time(&now);
	nowValues = gmtime(&now);
	/*
	ds = dsm_read("hal9000", "DSM_AS_CORRDATATYPE_L",
		      &dirCode, &timestamp);
	if (ds != DSM_SUCCESS) {
	  dsm_error_message(ds, "dsm_read (5)");
	  perror("writer: dsm read of DSM_AS_CORRDATATYPE_L");
	  dirCode = DD_SCIENCE;
	}
	*/
	dirCode = DD_ENGINEERING;

	/*
	ds = dsm_read("hal9000", "DSM_AS_C1_SOURCE_S",
		      &doubleBandwidthContinuum, &timestamp);
	if (ds != DSM_SUCCESS) {
	  dsm_error_message(ds, "dsm_read (6)");
	  perror("writer: dsm read of DSM_AS_C1_SOURCE_S");
	}
	*/
	if (doubleBandwidthContinuum != 1)
	  doubleBandwidthContinuum = FALSE;
	dprintf("doubleBandwidthContinuum = %d\n", doubleBandwidthContinuum);
	switch (dirCode) {
	case DD_GARBAGE:
	  sprintf(directory, "garbage");
	  break;
	case DD_PRIMING:
	  sprintf(directory, "priming");
	  break;
	case DD_ENGINEERING:
	  sprintf(directory, "engineering");
	  break;
	case DD_POINTING:
	  sprintf(directory, "pointing");
	  break;
	case DD_BASELINE:
	  sprintf(directory, "baseline");
	  break;
	case DD_FLUX:
	  sprintf(directory, "flux");
	  break;
	default:
	  sprintf(directory, "science");
	}
	sprintf(pathName, "/data/%s/mir_data/%02d%02d%02d_%02d:%02d:%02d/",
		directory,
		nowValues->tm_year - 100,
		nowValues->tm_mon + 1,
		nowValues->tm_mday,
		nowValues->tm_hour,
		nowValues->tm_min,
		nowValues->tm_sec);
	/* pathNameChanged = TRUE; */
	rCode = mkdir(pathName, 0777);
	if (rCode < 0) {
	  fprintf(stderr, "writer: Problem creating \"%s\", rCode = %d, errno = %d\n",
		  pathName, rCode, errno);
	  perror("writer: creating data directory");
	  exit(-1);
	}
	inhid = sphid = blhid = 0;
	/*
	  (re)Open the data files
	*/
	if (plotFileOpen)
	  for (rx = 0; rx < MAX_RX; rx++)
	    fclose(plotFile[rx]);
	if (baselineFileOpen)
	  fclose(baselineFile);
	if (weFileOpen)
	  fclose(weFile);
	if (tsysFileOpen)
	  fclose(tsysFile);
	if (codesFileOpen)
	  fclose(codesFile);
	if (engFileOpen)
	  fclose(engFile);
	if (inFileOpen)
	  fclose(inFile);
	if (spFileOpen)
	  fclose(spFile);
	if (schFileOpen)
	  fclose(schFile);
	if (!antFileWritten) {
	  sprintf(fileName, "%santennas", pathName);
	  antFile = fopen(fileName, "w");
	  if (antFile != NULL) {
	    int ii;

	    for (ii = 1; ii <= 10; ii++)
	      fprintf(antFile, "%d\t%16.9e\t%16.9e\t%16.9e\n", ii,
		      scanCopy.header.DDSdata.x[ii],
		      scanCopy.header.DDSdata.y[ii],
		      scanCopy.header.DDSdata.z[ii]);
	    fclose(antFile);
	  } else {
	    fprintf(stderr, "Error opening antenna positions file\n");
	    perror(fileName);
	  }
	  antFileWritten = TRUE;
	}
	if (!pIFileWritten) {
	  sprintf(fileName, "%sprojectInfo", pathName);
	  pIFile = fopen(fileName, "w");
	  if (pIFile != NULL) {
	    /* int ds; */
	    char pI[31];

	    /*
	    ds = dsm_read("hal9000", "DSM_AS_PROJECT_PI_C30",
		&pI[0], &timestamp);
	    if (ds != DSM_SUCCESS) {
	      dsm_error_message(ds, "dsm_read (7)");
	      perror("getDSMInfo: dsm read of DSM_AS_PROJECT_PI_C30");
	    } else {
	      pI[30] = (char)0;
	      fprintf(pIFile, "%s\n", pI);
	    }
	    */
	    strcpy(pI, "Jonathan");
	    fprintf(pIFile, "%s\n", pI);
	    fclose(pIFile);
	  } else {
	    fprintf(stderr, "Error opening PI file\n");
	    perror(fileName);
	  }
	  pIFileWritten = TRUE;
	}
	if (!codeVersionFileWritten) {
	  FILE *src, *dst;

	  src = fopen("/common/configFiles/codeVersions", "r");
	  if (src == NULL) {
	    fprintf(stderr, "Could not open source codeVersions file\n");
	  } else {
	    sprintf(fileName, "%scodeVersions", pathName);
	    dst = fopen(fileName, "w");
	    if (dst == NULL) {
	      fprintf(stderr, "Could not open destination codeVersions file\n");
	    } else {
	      while (TRUE) {
		char ch;

		ch= getc(src);
		if (ch == EOF)
		  break;
		else
		  putc(ch, dst);
	      }
	      fclose(dst);
	    }
	    fclose(src);
	  }
	}
	for (rx = 0; rx < MAX_RX; rx++)
	  if (receiverActive[rx]) {
	    if (!((rx != doubleBandwidthRx) && doubleBandwidth)) {
	      sprintf(fileName, "%splot_me_5_rx%d", pathName, rx);
	      plotFile[rx] = fopen(fileName, "w");
	      if (plotFile[rx] == NULL) {
		perror("writer: plotFile fopen");
		exit(ERROR);
	      } else
		plotFileOpen = TRUE;
	    }
	  }
	if (!modeFileWritten) {
	  sprintf(fileName, "%smodeInfo", pathName);
	  modeFile = fopen(fileName, "w");
	  if (modeFile != NULL) {
	    if (fullPolarization)
	      fprintf(modeFile, "2 1");
	    else if (doubleBandwidth)
	      fprintf(modeFile, "1 4\n");
	    else if (receiverActive[0] && receiverActive[1])
	      fprintf(modeFile, "2 2\n");
	    else
	      fprintf(modeFile, "1 2\n");
	    fclose(modeFile);
	  } else {
	    fprintf(stderr, "Error opening mode file\n");
	    perror(fileName);
	  }
	  modeFileWritten = TRUE;
	}
	sprintf(fileName, "%sbl_read", pathName);
	baselineFile = fopen(fileName, "w");
	if (baselineFile == NULL) {
	  perror("writer: baselineFile fopen");
	  exit(ERROR);
	} else
	  baselineFileOpen = TRUE;
	sprintf(fileName, "%swe_read", pathName);
	weFile = fopen(fileName, "w");
	if (weFile == NULL) {
	  perror("writer: weFile fopen");
	  exit(ERROR);
	} else
	  weFileOpen = TRUE;
	sprintf(fileName, "%stsys_read", pathName);
	tsysFile = fopen(fileName, "w");
	if (tsysFile == NULL) {
	  perror("writer: tsysFile fopen");
	  exit(ERROR);
	} else
	  tsysFileOpen = TRUE;
	sprintf(fileName, "%scodes_read", pathName);
	codesFile = fopen(fileName, "w");
	if (codesFile == NULL) {
	  perror("writer: codesFile fopen");
	  exit(ERROR);
	} else
	  codesFileOpen = TRUE;
	fixedCodes(codesFile);
	sprintf(fileName, "%seng_read", pathName);
	engFile = fopen(fileName, "w");
	if (engFile == NULL) {
	  perror("writer: engFile fopen");
	  exit(ERROR);
	} else
	  engFileOpen = TRUE;
	sprintf(fileName, "%sin_read", pathName);
	inFile = fopen(fileName, "w");
	if (inFile == NULL) {
	  perror("writer: inFile fopen");
	  exit(ERROR);
	} else
	  inFileOpen = TRUE;
	sprintf(fileName, "%ssp_read", pathName);
	spFile = fopen(fileName, "w");
	if (spFile == NULL) {
	  perror("writer: spFile fopen");
	  exit(ERROR);
	} else
	  spFileOpen = TRUE;
	sprintf(fileName, "%ssch_read", pathName);
	schFile = fopen(fileName, "w");
	if (schFile == NULL) {
	  perror("writer: schFile fopen");
	  exit(ERROR);
	} else
	  schFileOpen = TRUE;

	needNewDataFile = FALSE;
      } /* end of if (needNewDataFile) */

      numberOfSidebands = UNINITIALIZED;
      nCrates = nCratesReportingTime = 0;
      averageTime = 0.0;

      /* Write the Tsys data */
      if (store) {
	int ant;

	if (doubleBandwidth || (receiverActive[0] && receiverActive[1]))
	  tsysh.nMeasurements = 2;
	else
	  tsysh.nMeasurements = 2;
	tsysh.data = (float *)malloc(tsysh.nMeasurements*4*sizeof(float));
	if (tsysh.data == NULL) {
	  perror("tsysh.data");
	  exit(-1);
	}
	tsysh.data[0] = 4.0;
	tsysh.data[1] = 6.0;
	if (doubleBandwidth) {
	  tsysh.data[4] = 6.0;
	  tsysh.data[5] = 8.0;
	} else if (receiverActive[0] && receiverActive[1]) {
	  tsysh.data[4] = 4.0;
	  tsysh.data[5] = 6.0;
	} else {
	  tsysh.data[4] = 6.0;
	  tsysh.data[5] = 8.0;
	}
	for (ant = 1; ant <= 8; ant++) {
	  if (antennaInArray[ant]) {
	    antTsysByteOffset[ant] = tsysByteOffset;
	    if (receiverActive[0])
	      tsysh.data[2] = tsysh.data[3] = 2.0*scanCopy.header.antavg[ant].tsys;
	    else
	      tsysh.data[2] = tsysh.data[3] = 2.0*scanCopy.header.antavg[ant].tsys_rx2;
	    tsysh.data[2] = tsysh.data[3] = 50.0;
	    if (doubleBandwidth || (receiverActive[0] && receiverActive[1]))
	      tsysh.data[6] = tsysh.data[7] = 2.0*scanCopy.header.antavg[ant].tsys_rx2;
	    tsysh.data[6] = tsysh.data[7] = 50.0;
	    printf("...---... Ant %d Tsys: n: %d %f %f %f %f %f %f %f %f\n", ant, tsysh.nMeasurements,
		   tsysh.data[0], tsysh.data[1], tsysh.data[2], tsysh.data[3], tsysh.data[4], tsysh.data[5],
		   tsysh.data[6], tsysh.data[7]);
	    fwrite_unlocked(&tsysh.nMeasurements, 4, 1, tsysFile);
	    fwrite_unlocked(tsysh.data, tsysh.nMeasurements*16, 1, tsysFile);
	    tsysByteOffset += 4+tsysh.nMeasurements*16;
	  }
	}
	free(tsysh.data);
      }

      /*
      dSMStatus = dsm_read("hal9000", "DSM_ONLINE_ANTENNAS_V11_B", &antOnline[0], &timestamp);
      if (dSMStatus != DSM_SUCCESS)
        dsm_error_message(dSMStatus, "dsm_read (online antennas)");
      */
      {
	int i;

	for (i = 0; i < 11; i++)
	  antOnline[i] = TRUE;
      }
      /*
	Calculate pseudo-continuum channel amplitude, phase, coherence and average frequency
      */
      for (rx = 0; rx < MAX_RX+1; rx++) {
	nBands[rx] = 1;
	for (sb = 0; sb < MAX_SB; sb++)
	  for (pol = 0; pol < MAX_POLARIZATION; pol++) {
	    pCFreq[rx][sb][pol]        = pCVelo[rx][sb][pol] = NAN;
	    pCFreqSum[rx][sb][pol]     = pCVeloSum[rx][sb][pol] = 0.0;
	    pCFreqNPoints[rx][sb][pol] = 0;
	    for (ant1 = 0; ant1 < MAX_ANT+1; ant1++)
	      for (ant2 = 0; ant2 < MAX_ANT+1; ant2++) {
		pCAmp[rx][ant1][ant2][sb][pol] =
		  pCPhase[rx][ant1][ant2][sb][pol] =
		  pCCoh[rx][ant1][ant2][sb][pol] = NAN;
		pCAmpSum[rx][ant1][ant2][sb][pol] =
		  nPCCohPoints[rx][ant1][ant2][sb][pol] =
		  pCRealSum[rx][ant1][ant2][sb][pol] =
		  pCImagSum[rx][ant1][ant2][sb][pol] = 0.0;
		pCNPoints[rx][ant1][ant2][sb][pol] = 0;
	      }
	  }
	nChannels[rx][0] = 1; /* pseudo-continuum */
      }

      inhid = globalScanNumber;
      lowestCrateNumber = UNINITIALIZED;
      for (crate = 1; crate <= MAX_CRATE; crate++) {
	if (scanCopy.received[crate]) {
	  int block;

	  if (lowestCrateNumber == UNINITIALIZED) {
	    lowestCrateNumber = crate;
	    /*
	      We will need to know the total number of spectral chunks
	      later on.   Compute that here.   Note that we are
	      going to assume that all baselines have the same number of
	      spectral chunks
	    */
	    ant1N = scanCopy.data[lowestCrateNumber]->set.set_val[0].antennaNumber[1];
	    ant2N = scanCopy.data[lowestCrateNumber]->set.set_val[0].antennaNumber[2];
	    rxN   = 1 - scanCopy.data[lowestCrateNumber]->set.set_val[0].rxBoardHalf;
	  }
	  nCrates++;
	  if (crate < 12) {
	    averageTime += scanCopy.data[crate]->UTCtime;
	    nCratesReportingTime++;
	  }
	  
	  if (scanCopy.data[crate]->blockNumber < 4) {
	    setStart = 0; setStop = scanCopy.data[crate]->set.set_len; setInc = 1;
	  } else {
	    setStart = scanCopy.data[crate]->set.set_len - 1; setStop = -1; setInc = -1;
	  }
	  
	  /*
	    Loop twice through all the sets in this bundle.   The first
	    loop through sets just counts bands, and fills in the values
	    for arrays containing the number of channels per band, etc.
	  */
	  for (rxN = 0; rxN < MAX_RX; rxN++)
	    for (set = setStart; set != setStop; set += setInc) {
	      int state1, state2, pol;

	      state1 = scanCopy.data[crate]->set.set_val[set].antPolState[1];
	      state2 = scanCopy.data[crate]->set.set_val[set].antPolState[2];
	      if ((ant1N == scanCopy.data[crate]->set.set_val[set].antennaNumber[1]) &&
		  (ant2N == scanCopy.data[crate]->set.set_val[set].antennaNumber[2]) &&
		  (rxN == (1 - scanCopy.data[crate]->set.set_val[set].rxBoardHalf))) {
		if ((!fullPolarization) || ((state1 == 1) && (state2 == 1))) {
		  chunk = scanCopy.data[crate]->set.set_val[set].chunkNumber;
		  block = scanCopy.data[crate]->blockNumber;
		  bandIndx[rxN][nBands[rxN]] = sChunk(block, chunk);
		  if (chunkSName[rxN][nBands[rxN]] == UNINITIALIZED) {
		    if ((rxN == 0) || (!doubleBandwidth))
		      chunkSName[rxN][nBands[rxN]] = sChunk(block, chunk);
		    else
		      chunkSName[rxN][nBands[rxN]] = highSChunk(block, chunk);
		  }
		  nBands[rxN]++;
		  printf("nBands[%d] = %d\n", rxN, nBands[rxN]);
		}
	      }
	      if (numberOfSidebands == UNINITIALIZED)
		numberOfSidebands = scanCopy.data[crate]->set.set_val[set].real.real_len;
	      ant1 = scanCopy.data[crate]->set.set_val[set].antennaNumber[1];
	      ant2 = scanCopy.data[crate]->set.set_val[set].antennaNumber[2];
	      chunk = scanCopy.data[crate]->set.set_val[set].chunkNumber;
	      block = scanCopy.data[crate]->blockNumber;
	      rx = 1 - scanCopy.data[crate]->set.set_val[set].rxBoardHalf;
	      nChannels[rx][sChunk(block, chunk)] = 
		scanCopy.data[crate]->set.set_val[set].real.real_val[0].channel.channel_len;
	      if (fullPolarization) {
		chunk = scanCopy.data[crate]->set.set_val[set].chunkNumber;
		block = scanCopy.data[crate]->blockNumber;
		if (state1 == 0) {
		  if (state2 == 0)
		    pol = 0;
		  else
		    pol = 2;
		} else
		  if (state2 == 0)
		    pol = 3;
		  else
		    pol = 1;
		cSIndx[0][ant1][ant2][pol][sChunk(block, chunk)].crate =
		  cSIndx[1][ant1][ant2][pol][sChunk(block, chunk)].crate = crate;
		cSIndx[0][ant1][ant2][pol][sChunk(block, chunk)].set =
		  cSIndx[1][ant1][ant2][pol][sChunk(block, chunk)].set = set;
	      } else {
		cSIndx[rx][ant1][ant2][0][sChunk(block, chunk)].crate = crate;
		cSIndx[rx][ant1][ant2][0][sChunk(block, chunk)].set   = set;
	      }
	    } /* for set ... */
	  if (fullPolarization)
	    nBands[1] = 0;
	  
	  /*
	    Now loop through the sets a second time, and calculate the pseudo-continuum
	    channel.
	  */
	  for (rx = 0; rx < MAX_RX; rx++)
	    for (set = setStart; set != setStop; set += setInc) {
	      int state1, state2;

	      ant1 = scanCopy.data[crate]->set.set_val[set].antennaNumber[1];
	      ant2 = scanCopy.data[crate]->set.set_val[set].antennaNumber[2];
	      state1 = scanCopy.data[crate]->set.set_val[set].antPolState[1];
	      state2 = scanCopy.data[crate]->set.set_val[set].antPolState[2];
	      chunk = scanCopy.data[crate]->set.set_val[set].chunkNumber;
	      block = scanCopy.data[crate]->blockNumber;
	      if (fullPolarization) {
		if (state1 == 0)
		  if (state2 == 0)
		    pol = 0;
		  else
		    pol = 2;
		else
		  if (state2 == 0)
		    pol = 3;
		  else
		    pol = 1;
	      } else
		pol = 0;
	      for (sb = 0; sb < scanCopy.data[crate]->set.set_val[set].real.real_len; sb++) {
		int channel;
		float ampSum, realSum, imagSum, edgeWidth;

		ampSum = realSum = imagSum = 0.0;
		if (rx == 1 - scanCopy.data[crate]->set.set_val[set].rxBoardHalf) {
		  float channelWeight;
		  
		  /*
		    Make the pseudo-continuum from the entire available bandwidth
		  */
		  if (doubleBandwidth )
		    effRx = doubleBandwidthRx;
		  else if (fullPolarization)
		    effRx = 0;
		  else
		    effRx = rx;
		  effRx = 0;
		  goodChunk[rx][ant1][ant2][sChunk(block, chunk)] = TRUE;
		  if (goodChunk[rx][ant1][ant2][sChunk(block, chunk)] &&
		      ((rx == effRx) || (doubleBandwidth && doubleBandwidthContinuum) ||
		       ((rx == 1) && (!doubleBandwidth)))) {
		    pCFreqSum[effRx][sb][pol] += scanCopy.chunkFreq[rx][sb][sChunk(block, chunk)];
		    pCVeloSum[effRx][sb][pol] += scanCopy.chunkVelo[rx][sb][sChunk(block, chunk)];
		    pCFreqNPoints[effRx][sb][pol]++;
		    if (sChunk(block, chunk) < 49)
		      edgeWidth = (CHUNK_FULL_BANDWIDTH - CHUNK_USABLE_BANDWIDTH) / (2.0 * CHUNK_FULL_BANDWIDTH);
		    else
		      edgeWidth = (SWARM_CHUNK_FULL_BANDWIDTH - SWARM_CHUNK_USABLE_BANDWIDTH) / (2.0 * SWARM_CHUNK_FULL_BANDWIDTH);
		    startChannel =
		      (int)((float)scanCopy.data[crate]->set.set_val[set].real.real_val[sb].channel.channel_len
			    * edgeWidth);
		    endChannel =
		      (int)((float)scanCopy.data[crate]->set.set_val[set].real.real_val[sb].channel.channel_len
			    * (1.0 - edgeWidth));
		    /*
		      Calculate the weight each channel should have, to account
		      for differing numbers of channels in different chunks.
		    */
		    channelWeight = (float)(1 + endChannel - startChannel);
		    if (channelWeight < 1.0)
		      channelWeight = 1.0;
		    channelWeight = 1.0 / channelWeight;
		    dprintf("For chunk s%02d, start, %d, end %d, channelWeight = %f\n",
			    sChunk(block, chunk),startChannel, endChannel, channelWeight);
		    for (channel = startChannel; channel <= endChannel; channel++) {
		      float real, imag, amp;
		      
		      real = scanCopy.data[crate]->set.set_val[set].real.real_val[sb].channel.channel_val[channel];
		      if (isnan(real)) {
			/* printf("___ real is NAN at chan %d sb %d crate %d set %d\n", */
			/*        channel, sb, crate, set); */
			exit(-1);
		      } else
			/* printf("___ real is OK at chan %d sb %d crate %d set %d\n", */
			/*        channel, sb, crate, set); */
		      realSum += real * channelWeight;
		      imag = scanCopy.data[crate]->set.set_val[set].imag.imag_val[sb].channel.channel_val[channel];
		      imagSum += imag * channelWeight;
		      amp = sqrt(real*real + imag*imag) * channelWeight;
		      ampSum += amp;
		    }
		    pCRealSum[effRx][ant1][ant2][sb][pol] += realSum;
		    pCImagSum[effRx][ant1][ant2][sb][pol] += imagSum;
		    pCAmpSum[effRx][ant1][ant2][sb][pol]  += ampSum;
		    nPCCohPoints[effRx][ant1][ant2][sb][pol]  += 1.0;
		    /*
		      pCNPoints[effRx][ant1][ant2][sb][pol] += endChannel - startChannel + 1;
		    */
		    pCNPoints[effRx][ant1][ant2][sb][pol]++; /* Just normalize by the number of chunks summed */
		  } else
		    printf("Ignoring chunk s%d on baseline %d-%d for pc\n",
			    sChunk(block, chunk), ant1, ant2);
		  dprintf("pCFreqSum[%d][%d][%d] = %f\n", rx, sb, pol, pCFreqSum[effRx][sb][pol]);
		} /* if rx == 1 - blah blah blah  */
	      } /* For sb ... */
	    } /* for set ... */
	} /* if scanCopy.received[crate] */
      } /* for crate... */
      averageTime /= (3600.0*(float)nCratesReportingTime);
      averageTime = 0.0;
      for (rx = 0; rx < MAX_RX; rx++) {
	for (pol = 0; pol < MAX_POLARIZATION; pol++) {
	  if (doubleBandwidth)
	    effRx = doubleBandwidthRx;
	  else if (fullPolarization)
	    effRx = 0;
	  else
	    effRx = rx;
	  effRx = 0;
	  dprintf("nBands[%d] will be %d\n", rx, nBands[rx]);
	  for (sb = 0; sb < numberOfSidebands; sb++) {
	    if (pCFreqNPoints[effRx][sb][pol] != 0) {
	      pCFreq[effRx][sb][pol] = pCFreqSum[effRx][sb][pol] / (double)pCFreqNPoints[effRx][sb][pol];
	      pCVelo[effRx][sb][pol] = pCVeloSum[effRx][sb][pol] / (double)pCFreqNPoints[effRx][sb][pol];
	    }
	    for (ant1 = 1; ant1 < MAX_ANT+1; ant1++)
	      for (ant2 = 1; ant2 < MAX_ANT+1; ant2++) {
		if (pCNPoints[effRx][ant1][ant2][sb][pol] != 0) {
		  float realAve, imagAve;

		  realAve = pCRealSum[effRx][ant1][ant2][sb][pol] / (float)pCNPoints[effRx][ant1][ant2][sb][pol];
		  imagAve = pCImagSum[effRx][ant1][ant2][sb][pol] / (float)pCNPoints[effRx][ant1][ant2][sb][pol];
		  if (doubleBandwidth && (rx == effRx))
		    pCAmpSum[effRx][ant1][ant2][sb][pol] /= (float)pCNPoints[effRx][ant1][ant2][sb][pol];
		  pCAmp[effRx][ant1][ant2][sb][pol] = sqrt(realAve*realAve + imagAve*imagAve);
		  pCPhase[effRx][ant1][ant2][sb][pol] = atan2(imagAve, realAve) * RADIANS_TO_DEGREES;
		  if (pCAmpSum[effRx][ant1][ant2][sb][pol] != 0.0)
		    pCCoh[effRx][ant1][ant2][sb][pol] = pCAmp[effRx][ant1][ant2][sb][pol] / pCAmpSum[effRx][ant1][ant2][sb][pol];
		  else
		    pCCoh[effRx][ant1][ant2][sb][pol] = NAN;
		}
	      }
	  }
	}
      }
      numberOfBaselines = numberOfReceivers = 0;
      for (i = 0; i <= MAX_RX; i++)
	if (receiverActive[i])
	  numberOfReceivers++;

      weh.scanNumber   = globalScanNumber;
      weh.flags[0]     = 0;
      weh.N[0]         = scanCopy.header.antavg[lowestAntennaNumber].N = 1.0;
      weh.Tamb[0]      = scanCopy.header.antavg[lowestAntennaNumber].Tamb = 20.0;;
      weh.pressure[0]  = scanCopy.header.antavg[lowestAntennaNumber].pressure = 800.0;
      weh.humid[0]     = scanCopy.header.antavg[lowestAntennaNumber].humid = 10.0;
      weh.windSpeed[0] = scanCopy.header.antavg[lowestAntennaNumber].windSpeed = 0.0;
      weh.windDir[0]   = scanCopy.header.antavg[lowestAntennaNumber].windDir = 0.0;
      for (i = 1; i <= MAX_ANT; i++) {
	weh.flags[i]     = scanCopy.header.antavg[i].isvalid[1];
	weh.N[i]         = scanCopy.header.antavg[i].N = 1.0;
	weh.windSpeed[i] = weh.windDir[i] = weh.h2o[i] =
	  weh.Tamb[i] = weh.pressure[i] =
	  weh.humid[i] = -1.0;
      }
      if (store)
	fwrite_unlocked(&weh, sizeof(weh), 1, weFile);
      /*
	Write stuff to codes file
      */
      jD = scanCopy.header.antavg[lowestAntennaNumber].tjd + 0.5;
      {
	int year, mon, day, hh, mm, ss;
	time_t now;
	struct tm *nowValues;

	time(&now);
	nowValues = gmtime(&now);
	year = nowValues->tm_year+1900;
	mon = nowValues->tm_mon+1;
	day = nowValues->tm_mday;
	hh = nowValues->tm_hour;
	mm = nowValues->tm_min;
	ss = nowValues->tm_sec;
	jD = calcJD(year, mon, day, hh, mm, ss) + 0.5;
	averageTime = (double)hh + ((double)mm)/60.0 + ((double)ss)/3600.0;
      }
      caldat(jD, &mon, &day, &yr);
      hr = averageTime;
      min = (averageTime - hr)*60.0;
      sec = (averageTime -  hr - min/60.0)*3600.0; 

      /* Keto Komment: Check if the current day has changed from the reference day. 
	 This will happen when the UT passes 24 hrs. On the first
	 integration, the reference day will be set here */
      if (jD != refJD) {
	dprintf("Day changed: julianday %ld refday %ld\n", jD, refJD);
	iRefTime++;
	refJD = jD;
	strcpy(codeh.v_name, "ref_time");
	codeh.icode = iRefTime;
	codeh.ncode = 0;
	refTimeStr(mon, day, yr, codeh.code);
	if (store)
	  fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
	dprintf("The new reference day is %s\n", codeh.code);
      }
      /* Write the UTS time to the code file */
      strcpy(codeh.v_name, "ut");
      codeh.icode = globalScanNumber;
      uTTimeStr(mon, day, yr, hr, min, sec, codeh.code);
      if (store)
	fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
      strcpy(utstring, codeh.code);

      /* Write "vrad" string to the code file */
      strcpy(codeh.v_name, "vrad");
      codeh.icode = globalScanNumber;
      vRadial = scanCopy.header.loData.vRadial = 0.0;
      if (fabs(vRadial) <= 100000000.0)
	sprintf(codeh.code, "%12.1f", vRadial);
      else
	sprintf(codeh.code, "%21.14e", vRadial);
      codeh.ncode = strlen(codeh.code);
      if (store)
	fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
      thisVRadId++;

      /*
	Go through the source list to see if we already have written
	the source name in this data set
      */
      strcpy(scanCopy.header.antavg[lowestAntennaNumber].sourceName, globalSourceName);
      for (source = 0; source < nSources; source++)
	if (strncmp(sourceList[source],
		    scanCopy.header.antavg[lowestAntennaNumber].sourceName, 25) == 0)
	  break;

      /* If there was no match, 
	 update source list and write new code header */
      if ((source == nSources) || (nSources == 0)) {
	strcpy(sourceList[nSources],
	       scanCopy.header.antavg[lowestAntennaNumber].sourceName);
	nSources++;
	strcpy(codeh.v_name, "source");
	codeh.icode = nSources;
	strncpy(codeh.code,
		scanCopy.header.antavg[lowestAntennaNumber].sourceName, 25);
	printf("...---... Writing source name \"%s\" (\"%s\")\n", codeh.code, globalSourceName);
	if (store)
	  fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
      }
      thisSourceId = source+1;
      /* 
	 Put in a new ascii code RA and Dec for each integration. 
	 If it is not a planet, RA and Dec will not
	 change, write them anyway because it is easier for the moment.
      */
      
      ira = idec = globalScanNumber;
      rar = scanCopy.header.antavg[lowestAntennaNumber].ra_j2000 * HOURS_TO_RADIANS;
      rar = 1.0;
      decr = scanCopy.header.antavg[lowestAntennaNumber].dec_j2000 * DEGREES_TO_RADIANS;
      decr = 0.5;
      codeh.icode = globalScanNumber;
      strcpy(codeh.v_name, "ra");
      coordStr(rar, codeh.code, 0);
      if (store)
	fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
      strcpy(codeh.v_name, "dec");
      coordStr(decr, codeh.code, 1);
      if (store)
	fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);

      strncpy(currentSource, scanCopy.header.antavg[lowestAntennaNumber].sourceName, 23);
      currentSource[23] = (char)0;
      hAMidpoint = calculateUVW(&scanCopy, averageTime, jD) * RADIANS_TO_HOURS;
      /*
	Write plot file stuff - this is the stuff corrPlotter reads, and it has
	nothing to do with the mir date files.
      */
      for (rx = 0; rx < MAX_RX; rx++) {
	int ii, ePol;

	if (fullPolarization)
	  if (rx == 0)
	    ePol = 1;
	  else
	    ePol = 0;
	else
	  ePol = 0;
	polarInt = 0;
	if (scanCopy.dSMStuff.polarMode == 1)
	  for (ii = 1; ii <= 10; ii++)
	    switch(scanCopy.dSMStuff.polarStates[ii]) {
	    case 'R':
	      polarInt += 1 << (3*ii);
	      break;
	    case 'L':
	      polarInt += 2 << (3*ii);
	      break;
	    case 'V':
	      polarInt += 3 << (3*ii);
	      break;
	    case 'H':
	      polarInt += 4 << (3*ii);
	      break;
	    }
	if (doubleBandwidth)
	  effRx = doubleBandwidthRx;
	else if (fullPolarization)
	  effRx = 0;
	else
	  effRx = rx;
	effRx = 0;
	{
	  int ii;

	  for (ii = 1; ii < 5; ii++)
	    scanCopy.header.antavg[ii].isvalid[0] = 1;
	}
	if (receiverActive[rx]) {
	  if (store && (!((rx != effRx) && doubleBandwidth))) {
	    fprintf(plotFile[rx], "%s ", scanCopy.header.antavg[lowestAntennaNumber].sourceName);
	    fprintf(plotFile[rx], "%f %f %f %f %d ", averageTime, hAMidpoint, decr,
		    (pCFreq[effRx][0][ePol]+pCFreq[effRx][1][ePol])/bDAIFSep,
		    scanCopy.header.antavg[lowestAntennaNumber].obstype);
	  }
	  for (sb = 0; sb < numberOfSidebands; sb++) {
	    for (ant1 = 1; ant1 < MAX_ANT+1; ant1++) {
	      for (ant2 = 1; ant2 < MAX_ANT+1; ant2++) {
		printf("...---... isvalid test %d %d %d %d\n", ant1, ant2, scanCopy.header.antavg[ant1].isvalid[0], (scanCopy.header.antavg[ant2].isvalid[0]));
		if ((scanCopy.header.antavg[ant1].isvalid[0] != 1) ||
		    (scanCopy.header.antavg[ant2].isvalid[0] != 1))
		  flag = -1;
		else
		  flag = 1;
		printf("...---... Amp check: pCAmp[%d][%d][%d][%d][%d] = %f\n",
		       effRx, ant1, ant2, sb, ePol, pCAmp[effRx][ant1][ant2][sb][ePol]);
		if ((ant1 < ant2) &&
		    (pCAmp[effRx][ant1][ant2][sb][ePol] == pCAmp[effRx][ant1][ant2][sb][ePol])) {
		  /*
		    In double bandwidth mode, if a spectral line is not being used for the
		    pseudo-continuum, average the two "receivers" pseudo-continua, because they
		    are two halves of the total bandwidth.   The average is stuck in the
		    low receiver pseudo-continuum channel.
		  */
		  if (doubleBandwidth && (rx == effRx) && FALSE) {
		    double r0, r1, i0, i1, rt, it;

		    r0 = pCAmp[0][ant1][ant2][sb][ePol] * cos(pCPhase[0][ant1][ant2][sb][ePol]);
		    r1 = pCAmp[1][ant1][ant2][sb][ePol] * cos(pCPhase[1][ant1][ant2][sb][ePol]);
		    i0 = pCAmp[0][ant1][ant2][sb][ePol] * sin(pCPhase[0][ant1][ant2][sb][ePol]);
		    i1 = pCAmp[1][ant1][ant2][sb][ePol] * sin(pCPhase[1][ant1][ant2][sb][ePol]);
		    rt = (r0 + r1)*0.5;
		    it = (i0 + i1)*0.5;
		    pCAmp[0][ant1][ant2][sb][ePol] = sqrt((rt*rt) + (it*it));
		    pCPhase[0][ant1][ant2][sb][ePol] = atan2(it, rt);
		  }
		  printf("...---... Before test %d %d %d %d   %d \n", store, rx, effRx, doubleBandwidth,
			 store && (!((rx != effRx) && doubleBandwidth)));
		  if (store && (!((rx != effRx) && doubleBandwidth)))
		    fprintf(plotFile[rx], "%d %d %d %e %e %e ",
			    ant1, ant2, flag,
			    pCAmp[effRx][ant1][ant2][sb][ePol],
			    pCPhase[effRx][ant1][ant2][sb][ePol],
			    pCCoh[effRx][ant1][ant2][sb][ePol]);
		  numberOfBaselines++;
		}
	      } /* for (ant2 = 1; ant2 < MAX_ANT+1; ant2++) */
	    } /* for (ant1 = 1; ant1 < MAX_ANT+1; ant1++) */
	  } /* for (sb = 0; sb < numberOfSidebands; sb++) */
	  if (store && (!((rx != effRx) && doubleBandwidth)))
	    fprintf(plotFile[rx], " %08x\n", polarInt);
	}
      } /* End of loop over rx */
      /*
	In double bandwidth mode, if a spectral line is not being used for the
	pseudo-continuum, average the two "receivers" pseudo-continua, because they
	are two halves of the total bandwidth.
      */
      numberOfBaselines /= (numberOfSidebands*numberOfReceivers);
      /*
	Write the engineering data file stuff
      */
      for (crate = 0; crate <= MAX_CRATE; crate++) {
	if (scanCopy.received[crate]) {
	  if (scanCopy.data[crate]->blockNumber < 4) {
	    setStart = 0; setStop = scanCopy.data[crate]->set.set_len; setInc = 1;
	  } else {
	    setStart = scanCopy.data[crate]->set.set_len - 1; setStop = -1; setInc = -1;
	  }
	  for (set = setStart; set != setStop; set += setInc) {
	    int found;
	    
	    ant1 = scanCopy.data[crate]->set.set_val[set].antennaNumber[1];
	    found = FALSE;
	    i = 0;
	    while ((i < nAntennas) && (!found)) {
	      if (ant1 == foundAntennaList[i])
		found = TRUE;
	      else
		i++;
	    }
	    if (!found)
	      foundAntennaList[nAntennas++] = ant1;
	    ant2 = scanCopy.data[crate]->set.set_val[set].antennaNumber[2];
	    found = FALSE;
	    i = 0;
	    while ((i < nAntennas) && (!found)) {
	      if (ant2 == foundAntennaList[i])
		found = TRUE;
	      else
		i++;
	    }
	    if (!found)
	      foundAntennaList[nAntennas++] = ant2;
	  }
	}
      }
      if (store)
	for (ant1 = 0; ant1 < nAntennas; ant1++)
	  writeEngData(foundAntennaList[ant1],
		       scanCopy.padList[foundAntennaList[ant1]],
		       scanCopy.header.antavg[foundAntennaList[ant1]],
		       engFile);
      
      /*
	Write the baseline file stuff - this is a mir file.
      */
      
      /* Create an index of baselines: baseline[i] -> ant[j]-ant[k] */
      bl = 0;
      for (sb = 0; sb < numberOfSidebands; sb++)
	for (ant1 = 1; ant1 < MAX_ANT+1; ant1++)
	  for (ant2 = 1; ant2 < MAX_ANT+1; ant2++)
	    if ((ant2 > ant1) &&
		((pCAmp[0][ant1][ant2][sb][0] == pCAmp[0][ant1][ant2][sb][0]) ||
		 (pCAmp[1][ant1][ant2][sb][0] == pCAmp[1][ant1][ant2][sb][0]))) {
	      bslnIndx[bl].sb   = sb;
	      bslnIndx[bl].ant1 = ant1;
	      bslnIndx[bl].ant2 = ant2;
	      reverseBslnIndx[ant1][ant2][sb] = reverseBslnIndx[ant2][ant1][sb] = bl;
	      bl++;
	    }

      dprintf("Before writing baseline codes, numberOfBaselines = %d\n", numberOfBaselines);
      /* Write out any new baseline codes we need to */
      for (bl = 0; bl < numberOfBaselines; bl++)
	if (bslnIndx[bl].code == UNINITIALIZED) {
	  codeh.ncode = codeh.icode = bslnIndx[bl].code = nBaselineCodes++;
	  strcpy(codeh.v_name, "blcd"); 
	  sprintf(codeh.code, "%d-%d", bslnIndx[bl].ant1, bslnIndx[bl].ant2);
	  if (store)
	    fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
	}
      
      /* Write our any new spectral chunk codes we need to */
      for (crate = 0; crate <= MAX_CRATE; crate++) {
	if (scanCopy.received[crate]) {
	  int sch;

	  if (scanCopy.data[crate]->blockNumber < 4) {
	    setStart = 0; setStop = scanCopy.data[crate]->set.set_len; setInc = 1;
	  } else {
	    setStart = scanCopy.data[crate]->set.set_len - 1; setStop = -1; setInc = -1;
	  }
	  for (sch = 1; sch <= 24; sch++) {
	    if (chunkCodes[0][sch] == UNINITIALIZED) {
	      chunkCodes[0][sch] = nChunkCodes++;
	      if (!doubleBandwidth)
		chunkCodes[1][sch] = chunkCodes[0][sch];
	      strcpy(codeh.v_name, "band"); 
	      codeh.icode = codeh.ncode = chunkCodes[0][sch];
	      sprintf(codeh.code, "s%02d", sch);
	      if (store)
		fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
	      dprintf("Setting chunkCodes[%d][%d] = %d, code = \"%s\"\n", 0, sch, chunkCodes[0][sch], codeh.code);
	    }
	  }
	  if (doubleBandwidth || TRUE) {
	    if (doubleBandwidthOffset == 0)
	      doubleBandwidthOffset = nChunkCodes - 1;
	    for (sch = 25; sch <= 48; sch++) {
	      if (chunkCodes[1][sch] == UNINITIALIZED) {
		chunkCodes[1][sch] = nChunkCodes++;
		strcpy(codeh.v_name, "band"); 
		codeh.icode = codeh.ncode = chunkCodes[1][sch];
		sprintf(codeh.code, "s%02d", sch);
		if (store)
		  fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
		dprintf("Setting chunkCodes[%d][%d] = %d, code = \"%s\"\n", 0, sch, chunkCodes[1][sch], codeh.code);
	      }
	    }
	  }
	  for (sch = 48; sch <= 50; sch++) {
	    if (chunkCodes[1][sch] == UNINITIALIZED) {
	      chunkCodes[1][sch] = nChunkCodes++;
	      strcpy(codeh.v_name, "band"); 
	      codeh.icode = codeh.ncode = chunkCodes[1][sch];
	      sprintf(codeh.code, "s%02d", sch);
	      if (store)
		fwrite_unlocked(&codeh, sizeof(codehDef), 1, codesFile);
	    }
	  }
	}
      }
      /*
	Calculate the offset for the spectra.   Each spectrum, including the
	pseudo-continuum, resides in a portion of sch.packdata.   So if,
	for example, you had 1 receiver with 256 channels on all chunks,
	and all eight antennas in the array, the size of the packdata
	structure would be:

	[2 * 28 * 24 * [(256 * 2 * 2) + (1 * 2)]] + [2 * 28 * [(2 * 2) + (1 * 2)]]
         ^    ^    ^      ^    ^   ^     ^   ^       ^    ^     ^   ^     ^   ^
         |    |    |      |    |   |     |   |       |    |     |   |     |   |

         S    B    B      C    R   2     E   2       S    B     R   2     E   2
         i    a    a      h    e         x           i    a     e         x
         d    s    n      a    a   B     t   B       d    s     a   B     t   B
         e    e    d      n    l   y     r   y       e    e     l   y     r   y
         b    l    s      n    +   t     a   t       b    l     +   t     a   t
         a    i           e    I   e         e       a    i     I   e         e
         n    n           l    m   s     S   s       n    n     m   s     S   s
         d    e           s    a   /     h   /       d    e     a   /     h   /
         s    s                g   S     o   S       s    s     g   S     o   S
                                   h     r   h                      h     r   h
                                   o     t   o                      o     t   o
                                   r     s   r                      r     s   r
                                   t         t                      t         t

      Here are the original Keto Komments about this (although I'm not using
      Eric's code for the manipulation):

         Calculate some numbers used in writing out the data  and set up the sch_read header
	 Calculate the total number of bytes to write and the byte offsets for each spectrum.
	 The output data is written as a 1D array with all the spectra written
	 one after the other. The spectra are identified by the byte offset
	 from the beginning of the array. This offset is written into the spectrum
	 header so that the IDL program knows which header goes with which spectrum
	 in the array.
	 Calculate the starting byte of each spectrum to be be written into the
	 1D array packdata. For each spectrum there will be a brief header of
	 1 short (WAS 5 SHORTS IN KETO KOMMENTS)
	 then 2 shorts (real and imag) for the complex visibility in each channel.
	 Then multiply by 2 to get the number of bytes.
	 This number of bytes is the sph[sb][is].dataoff parameter which the idl code uses
	 to find the starting position of the data for each spectrum.
	 sch.nbyt is the total number of bytes to be written. Since each number is a
	 short, sch.nbyt/2 is the dimension of the array packdata.
	 
	 ioffset[sb][ib][is] is an array which contains the offsets to be put into each
	 sph[sb][is].dataoff for each baseline.  ioffset is units of shorts (2 bytes) rather
	 than bytes.
      */
      
      sch.nbyt = 0;
      dprintf("Before loop, rA[0]:%d  rA[1]: %d rA[2]: %d\n",
	      receiverActive[0],
	      receiverActive[1],
	      receiverActive[2]);
      dprintf("nOS: %d  nP: %d  nB: %d  dB: %d\n",
	     numberOfSidebands, numberOfPolarizations, numberOfBaselines, doubleBandwidth);
      if (doubleBandwidth) {
	i1Stop = numberOfSidebands;
	i2Stop = numberOfPolarizations;
	i3Stop = numberOfBaselines;
	i4Stop = MAX_RX+1;
      } else {
	i1Stop = MAX_RX+1;
	i2Stop = numberOfSidebands;
	i3Stop = numberOfPolarizations;
	i4Stop = numberOfBaselines;
      }
      for (ind1 = 0; ind1 < i1Stop; ind1++)
	for (ind2 = 0; ind2 < i2Stop; ind2++)
	  for (ind3 = 0; ind3 < i3Stop; ind3++)
	    for (ind4 = 0; ind4 < i4Stop; ind4++) {
	      if (doubleBandwidth) {
		sb  = ind1;
		pol = ind2;
		bl  = ind3;
		rx  = ind4;
	      } else {
		rx  = ind1;
		sb  = ind2;
		pol = ind3;
		bl  = ind4;
	      }
	      if (receiverActive[rx])
		for (band = 0; band < nBands[rx]; band++)
		  if (!((rx == 1) && (band == 0) && doubleBandwidth)) {
		    specOffset[rx][sb][pol][bl][band] = sch.nbyt / sizeof(short);
		    if ((nChannels[rx][bandIndx[rx][band]] < 0) || (nChannels[rx][bandIndx[rx][band]] > N_SWARM_CHUNK_POINTS))
		      fprintf(stderr, "nChannels[%d][%d] = %d - nothing good will come from that!\n",
			      rx, band, nChannels[rx][bandIndx[rx][band]]);
		    sch.nbyt += sizeof(short)*(2*nChannels[rx][bandIndx[rx][band]] + 1);
		  }
	    }
      sch.packdata = (short *)malloc(sch.nbyt);
      if (sch.packdata == NULL) {
	fprintf(stderr, "Trying to malloc %d bytes\n", sch.nbyt);
	perror("malloc of sch.packdata");
	sch.packdata[2000000] = 0; /* Should force core dump */
	exit(-1); /* Die if the core dump doesn't happen */
      }
      sch.inhid = inhid;
      /*
	Here I set the values for items which we are not really using in the Mir format,
	at least for now.   Extracting this stuff should make the code clearer.
      */
      for (rx = 0; rx < MAX_RX; rx++)
	for (sb = 0; sb < MAX_SB; sb++)
	  for (bl = 0; bl < 2*MAX_BASELINE; bl++) {
	    blh[rx][sb][bl].spareint1 = blh[rx][sb][bl].spareint2 = blh[rx][sb][bl].spareint3  = 
	      blh[rx][sb][bl].spareint4 = blh[rx][sb][bl].spareint5  = blh[rx][sb][bl].spareint6  = 
	      0; /* Spare integers for future use */
	    blh[rx][sb][bl].sparedbl1 = blh[rx][sb][bl].sparedbl2 = blh[rx][sb][bl].sparedbl3  = 
	      blh[rx][sb][bl].sparedbl4 = blh[rx][sb][bl].sparedbl5  = blh[rx][sb][bl].sparedbl6  = 
	      0.0; /* Spare doubles for future use */
	  }
      sph.ipstate  = 0;                      /*  pol state int code        */
      sph.tau0     = 0.1; /*  velocity type int code    */
      sph.inhid    = globalScanNumber;       /*  integration id #          */
      sph.nrec     = 1;                      /*  # of records w/i inh#     */

      if (doubleBandwidth) {
	printf("Double bandwidth\n");
	i1Stop = numberOfSidebands;
	i2Stop = numberOfPolarizations;
	i3Stop = numberOfBaselines;
	i4Stop = MAX_RX;
      } else {
	printf("NOT double bandwidth\n");
	i1Stop = MAX_RX;
	i2Stop = numberOfSidebands;
	i3Stop = numberOfPolarizations;
	i4Stop = numberOfBaselines;
      }
      printf("*********** %d, %d, %d, %d\n", i1Stop, i2Stop, i3Stop, i4Stop);
      for (ind1 = 0; ind1 < i1Stop; ind1++) {
	int effectiveRx, firstBand, stopBand, bandInc;
	float u, v, baselineTsys;

	for (ind2 = 0; ind2 < i2Stop; ind2++) {
	  for (ind3 = 0; ind3 < i3Stop; ind3++) {
	    for (ind4 = 0; ind4 < i4Stop; ind4++) {

	      if (doubleBandwidth) {
		sb  = ind1;
		pol = ind2;
		bl  = ind3;
		rx  = ind4;
		effectiveRx = doubleBandwidthRx;
	      } else {
		rx  = ind1;
		sb  = ind2;
		pol = ind3;
		bl  = ind4;
		if (fullPolarization)
		  effectiveRx = 0;
		else
		  effectiveRx = rx;
	      }
	      lambda = calcLambda(pCFreq[effectiveRx][sb][0]);
	      if (receiverActive[rx] || doubleBandwidth) {
  		if (!doubleBandwidth || (rx == doubleBandwidthRx))
		  blhid++;
		ant1 = bslnIndx[bl].ant1;
		ant2 = bslnIndx[bl].ant2;
		
		blh[rx][sb][bl].blhid     = blhid; /*    proj. baseline id #       */
		blh[rx][sb][bl].inhid     = inhid; /*    integration id #          */
		blh[rx][sb][bl].isb       = sb;    /*    sideband int code         */
		blh[rx][sb][bl].ipol      = polarStateCode(pol, scanCopy.dSMStuff, ant1, ant2);
		if ((!fullPolarization) && ((scanCopy.header.antavg[ant1].obstype &
					     SRC_TYPE_FLUX_CALIBRATOR)))
		  blh[rx][sb][bl].ant1rx  = 1;
		if (fullPolarization) {
		  switch (pol) {
		  case 0:
		    blh[rx][sb][bl].ant1rx = 1; /* 400 x 400 */
		    blh[rx][sb][bl].ant2rx = 1;
		    break;
		  case 1:
		    blh[rx][sb][bl].ant1rx = 0; /* 345 x 345 */
		    blh[rx][sb][bl].ant2rx = 0;
		    break;
		  case 2:
		    blh[rx][sb][bl].ant1rx = 1; /* 400 x 345 */
		    blh[rx][sb][bl].ant2rx = 0;
		    break;
		  case 3:
		    blh[rx][sb][bl].ant1rx = 0; /* 345 x 400 */
		    blh[rx][sb][bl].ant2rx = 1;
		    break;
		  default:
		    fprintf(stderr, "Illegal pol state (%d) seen when setting ixqs\n", pol);
		    blh[rx][sb][bl].ant1rx = -1;
		    blh[rx][sb][bl].ant2rx = -1;
		  }
		} else
		  blh[rx][sb][bl].ant2rx = 0;
		if (scanCopy.dSMStuff.pointingMode == 0)
		  blh[rx][sb][bl].pointing = 1;     /* ipoint offset flag */
		else
		  blh[rx][sb][bl].pointing = 0;
		/* Set the receiver code */
		if (fullPolarization)
		  blh[rx][sb][bl].irec = 1; /* 345 GHz */
		else {
		  switch (scanCopy.header.antavg[ant1].rx[effectiveRx]) {
		  case 1: case 2:
		    blh[rx][sb][bl].irec = 0; /* 230 GHz */
		    break;
		  case 3: case 4:
		    blh[rx][sb][bl].irec = 1; /* 345 GHz */
		    break;
		  case 5: case 6:
		    blh[rx][sb][bl].irec = 2; /* 400 GHz */
		    break;
		  case 7:
		    blh[rx][sb][bl].irec = 3; /* 650 GHz */
		    break;
		  default:
		    blh[rx][sb][bl].irec = 0;
		  }
		}
		u = scanCopy.u[ant1][ant2]/lambda/1000.0;
		v = scanCopy.v[ant1][ant2]/lambda/1000.0;
		blh[rx][sb][bl].u = scanCopy.u[ant1][ant2]/lambda/1000.0;
		blh[rx][sb][bl].v = scanCopy.v[ant1][ant2]/lambda/1000.0;
		blh[rx][sb][bl].w = scanCopy.w[ant1][ant2]/lambda/1000.0;
		/*    projected baseline        */
		blh[rx][sb][bl].prbl = sqrtf(u*u + v*v);
		blh[rx][sb][bl].avedhrs = averageTime; /*    hrs offset from ref-time  */
		if (fullPolarization) {
		  if ((pCAmp[rx][ant1][ant2][sb][pol] == pCAmp[rx][ant1][ant2][sb][pol])
		   && (pCPhase[rx][ant1][ant2][sb][pol] ==  pCPhase[rx][ant1][ant2][sb][pol])
		   && (pCCoh[rx][ant1][ant2][sb][pol] == pCCoh[rx][ant1][ant2][sb][pol])) {
		    blh[rx][sb][bl].ampave  = pCAmp[rx][ant1][ant2][sb][pol];
		    blh[rx][sb][bl].phaave  = pCPhase[rx][ant1][ant2][sb][pol];
		    blh[rx][sb][bl].coh     = pCCoh[rx][ant1][ant2][sb][pol];
		  } else {
		    blh[rx][sb][bl].ampave = blh[rx][sb][bl].phaave = blh[rx][sb][bl].coh = 0.0;
		  }
		} else {
		  blh[rx][sb][bl].ampave  = pCAmp[rx][ant1][ant2][sb][pol];
		  blh[rx][sb][bl].phaave  = pCPhase[rx][ant1][ant2][sb][pol];
		  blh[rx][sb][bl].coh     = pCCoh[rx][ant1][ant2][sb][pol];
		}
		blh[rx][sb][bl].blsid   = bl;   /* physical baseline id # */
		blh[rx][sb][bl].iant1   = ant1; /* tel 1 int code         */
		blh[rx][sb][bl].iant2   = ant2; /* tel 2 int code         */
		blh[rx][sb][bl].ant1TsysOff = antTsysByteOffset[ant1];
		blh[rx][sb][bl].ant2TsysOff = antTsysByteOffset[ant2];
		blh[rx][sb][bl].iblcd   = reverseBslnIndx[ant1][ant2][0];
		/*    baseline int code         */
		/* Use local coords for MIR instead of geocentric from SMA */
		blh[rx][sb][bl].ble     = scanCopy.padE[ant1] - scanCopy.padE[ant2]; /*    bsl east vector  */
		blh[rx][sb][bl].bln     = scanCopy.padN[ant1] - scanCopy.padN[ant2]; /*    bsl north vector */
		blh[rx][sb][bl].blu     = scanCopy.padU[ant1] - scanCopy.padU[ant2]; /*    bsl up vector    */
    		if (store && (!doubleBandwidth || (rx == doubleBandwidthRx)))
		  fwrite_unlocked(&blh[rx][sb][bl], sizeof(blhDef), 1, baselineFile);
		
		if (doubleBandwidth) {
		  if (rx == 0) {
		    firstBand = 0;
		    stopBand = nBands[rx];
		    bandInc = 1;
		  } else {
		    firstBand = nBands[rx]-1;
		    stopBand = -1;
		    bandInc = -1;
		  }
		} else {
		  firstBand = 0;
		  stopBand = nBands[rx];
		  bandInc = 1;
		}
		sph.spareint1 = sph.spareint2 = sph.spareint3 = 
		  sph.spareint4 = sph.spareint5 = sph.spareint6 = htonl(0);
		for (band = firstBand; band != stopBand; band += bandInc) {
		  crate = cSIndx[rx][ant1][ant2][pol][bandIndx[rx][band]].crate;
		  if ((crate >= 1) && (crate <= 12)) {
		    sph.corrblock = scanCopy.data[crate]->blockNumber; /* Correlator block number */
		    sph.corrchunk = scanCopy.data[crate]->crateNumber; /* Correlator chunk number */
                                                                       /* (NOT sxx chunk number)  */
		  } else
		    sph.corrblock = sph.corrchunk = 0;
		  set   = cSIndx[rx][ant1][ant2][pol][bandIndx[rx][band]].set;
		  if (set > -1)
		    sph.corrblock = scanCopy.data[crate]->set.set_val[set].chunkNumber;
		  else
		    sph.corrchunk = 0; /* Indicates pseudo-continuum data */
		  if (band == 0) {
		    float pCReal, pCImag, amp, phase;
		    
		    amp   = pCAmp[0][ant1][ant2][sb][0];
		    phase = pCPhase[0][ant1][ant2][sb][0] * DEGREES_TO_RADIANS;
		    pCReal = amp*cos(phase);
		    pCImag = amp*sin(phase);
		    if (!((rx == 1) && doubleBandwidth)) {
		      if ((packData(1,
				    scanCopy.data[lowestCrateNumber]->intTime,
				    &pCReal,
				    &pCImag,
				    &sch.packdata[specOffset[rx][sb][0][bl][band]]) == -1) &&
			  (reportAllErrors)) {
			fprintf(stderr, "packdata returned error for cont channel rx: %d sb: %d bl: %d band: %d\n",
				rx, sb, bl, band);
		      }
		    }
		  } else {
		    if ((crate >= 0) && (set >= 0)) {
		      if ((packData(nChannels[rx][bandIndx[rx][band]],
				    scanCopy.data[lowestCrateNumber]->intTime,
				    &scanCopy.data[crate]->set.set_val[set].real.real_val[sb].channel.channel_val[0],
				    &scanCopy.data[crate]->set.set_val[set].imag.imag_val[sb].channel.channel_val[0],
				    &sch.packdata[specOffset[rx][sb][pol][bl][band]]) == -1) &&
			  (reportAllErrors)) {
			fprintf(stderr,
				"packdata returned error for set: %d crate: %d rx: %d sb: %d bl: %d band: %d\n",
				set, crate, rx, sb, bl, band);
		      }
		    }
		  }
		  sphid++;
		  sph.sphid   = sphid;             /*  spectrum id #             */
		  sph.blhid   = blh[rx][sb][bl].blhid; /*  proj. baseline id # (already in network order)      */
		  if (scanCopy.header.antavg[lowestAntennaNumber].obstype &
		      SRC_TYPE_GAIN_CALIBRATOR)  
		    sph.igq   = 1;
		  else
		    sph.igq   = 0;
		  if (scanCopy.header.antavg[lowestAntennaNumber].obstype &
		      SRC_TYPE_BANDPASS_CALIBRATOR)  
		    sph.ipq   = 1;
		  else
		    sph.ipq   = 0;
		  
		  if (band == 0)
		    sph.iband = 0; /*  spectral band int code    */
		  else
		    sph.iband = chunkSName[rx][band]; /*  spectral band int code    */
		  /*
		  if (sph.iband == 49)
		    sph.iband = 25;
		  if (sph.iband == 50)
		    sph.iband = 26;
		  */
		  /*  velocity (vtype)          */
		  /*  velocity res. km/s        */
		  /*  hardwired at 0 = vlsr     */
		  if (band == 0) {
		    sph.fsky  = pCFreq[effectiveRx][sb][pol]/1.0e9;
		    sph.vel   = 0.0;
		  } else {
		    if (nChannels[rx][bandIndx[rx][band]] == N_SWARM_CHUNK_POINTS) {
		      sph.fsky  = scanCopy.sWARMFreq[sb][bandIndx[rx][band]-49];
		      sph.vel   = 0.0;
		    } else {
		      sph.fsky  = scanCopy.chunkFreq[rx][sb][bandIndx[rx][band]]/1.0e9;
		      sph.vel   = 0.0;
		    }
		  }
		  /*  center sky freq. GHz */
		  if (nChannels[rx][bandIndx[rx][band]] == N_SWARM_CHUNK_POINTS) {
		    sph.vres = (-SPEED_OF_LIGHT * (SWARM_CHUNK_FULL_BANDWIDTH)/(float)nChannels[rx][bandIndx[rx][band]]
				* 1.0e-12) / sph.fsky;
		    sph.fres = (SWARM_CHUNK_FULL_BANDWIDTH * 1.0E-6)/(float)nChannels[rx][bandIndx[rx][band]];
		  } else {
		    sph.vres = (-SPEED_OF_LIGHT * (CHUNK_FULL_BANDWIDTH)/(float)nChannels[rx][bandIndx[rx][band]]
				* 1.0e-12) / sph.fsky;
		    sph.fres = (CHUNK_FULL_BANDWIDTH * 1.0E-6)/(float)nChannels[rx][bandIndx[rx][band]];
		  }
		  sph.gunnLO  = gunnLO[rx]/1.0e9;
		  if (band != 0) {
		    sph.cabinLO = 225.0;
		    sph.corrLO1 = corrLO1[chunkSName[rx][band]]/1.0e9;
		    sph.corrLO2 = corrLO2[chunkSName[rx][band]]/1.0e9;
		  } else {
		    sph.cabinLO = 0.0;
		    sph.corrLO1 = 5.0;
		    sph.corrLO2 = 0.0;
		  }
		  
		  if (band == 0) {
		    sph.vres *= 4.0;
		    sph.fres *= 4.0;
		  }
		  if (sb == 0) {
		    sph.fres *= -1.0;
		    sph.vres *= -1.0;
		  }
		  sph.integ  = scanCopy.data[lowestCrateNumber]->intTime; /*  integration time          */
		  /*
		    K L U D G E
		    
		    Mark Gurwell has found that the sky frequecies as calculated above are in error
		    by +-1/2 channel.   The following code "fixes" that problem, whose origion is
		    unknown.
		  */
		  sph.fsky   = sph.fsky + sidebandSign(rx, bandIndx[rx][band])*5.0e-4*sph.fres;
		  sph.vel    = sph.vel  + sidebandSign(rx, bandIndx[rx][band])*5.0e-1*sph.vres;
		  sph.fsky   = sph.fsky;
		  /* End of   K L U D G E   */

                  if (fullPolarization) {
                    switch (pol) {
                    case 0: /* hh */
                      baselineTsys = sqrt(scanCopy.header.antavg[ant1].tsys_rx2 *
					  scanCopy.header.antavg[ant2].tsys_rx2);
                      break;
                    case 1: /* vv */
                      baselineTsys = sqrt(scanCopy.header.antavg[ant1].tsys *
					  scanCopy.header.antavg[ant2].tsys);
                      break;
                    case 2: /* hv */
                      baselineTsys = sqrt(scanCopy.header.antavg[ant1].tsys_rx2 *
					  scanCopy.header.antavg[ant2].tsys);
                      break;
                    default: /* better be vh */
                      baselineTsys = sqrt(scanCopy.header.antavg[ant1].tsys *
					  scanCopy.header.antavg[ant2].tsys_rx2);
                    }
                  } else {
                    if (rx == 0) {
                      if ((scanCopy.header.antavg[ant1].tsys <= 0.0) &&
                          (scanCopy.header.antavg[ant2].tsys <= 0.0))
                        baselineTsys = 1000.0;
                      else if (scanCopy.header.antavg[ant1].tsys <= 0.0)
                        baselineTsys = scanCopy.header.antavg[ant2].tsys;
                      else if (scanCopy.header.antavg[ant2].tsys <= 0.0)
                        baselineTsys = scanCopy.header.antavg[ant1].tsys;
                      else
                        baselineTsys = sqrt(scanCopy.header.antavg[ant1].tsys *
					    scanCopy.header.antavg[ant2].tsys);
                    } else {
                      if ((scanCopy.header.antavg[ant1].tsys_rx2 <= 0.0) &&
                          (scanCopy.header.antavg[ant2].tsys_rx2 <= 0.0))
                        baselineTsys = 1000.0;
                      else if (scanCopy.header.antavg[ant1].tsys_rx2 <= 0.0)
                        baselineTsys = scanCopy.header.antavg[ant2].tsys_rx2;
                      else if (scanCopy.header.antavg[ant2].tsys_rx2 <= 0.0)
                        baselineTsys = scanCopy.header.antavg[ant1].tsys_rx2;
                      else
                        baselineTsys = sqrt(scanCopy.header.antavg[ant1].tsys_rx2 *
					    scanCopy.header.antavg[ant2].tsys_rx2);
                    }
                  }
		  if (isnan(baselineTsys))
                    baselineTsys = 1000.0;

                  if ((baselineTsys < 1.0) || (baselineTsys > 99999.9))
                    baselineTsys = 99999.9;
		  
                  if ((scanCopy.header.antavg[ant1].isvalid[0] != 1) ||
                      (scanCopy.header.antavg[ant2].isvalid[0] != 1) || spoilScanFlag) {
                    flag = -1;
                    if (antOnline[ant1] && antOnline[ant2])
                      thisScanWasGood = FALSE;
                  } else
                    flag = 1;

		  sph.wt        = 1.0;
		  sph.flags     = weh.flags[bslnIndx[bl].ant1] | weh.flags[bslnIndx[bl].ant2];
		  if (spoilScanFlag)
		    sph.flags |= SFLAG_SOURCE_CHANGE;
		  sph.flags = 0; /* All lab data flagged good */
		  sph.integ     = 30.0;
		  sph.vradcat   = scanCopy.header.loData.vCatalog;
		  sph.nch       = nChannels[rx][bandIndx[rx][band]]; /* # channels in spectrum */
		  sph.dataoff   = specOffset[rx][sb][pol][bl][band] * sizeof(short); /* byte offset for data   */
		  if (doubleBandwidth && (isnan(sph.rfreq) || (sph.rfreq == 0.0))) {
		    if (rx == 0)
		      sph.rfreq     = scanCopy.header.loData.restFrequency[1]/1.0e9;
		    else
		      sph.rfreq     = scanCopy.header.loData.restFrequency[0]/1.0e9;
		  }
		  sph.rfreq     = 230.538;
		  if (store && ((!((rx == 1) && (band == 0))) || (!doubleBandwidth)) ) {
		    fwrite_unlocked(&sph, sizeof(sphDef), 1, spFile);
		  }
		} /* End loop over bands */
	      } /* End of loop over baselines */
	    } /* End of loop over polarizations */
	  } /* End of loop over sidebands */
	} /* End of "if (receiverActive[rx] || doubleBandwidth)" */
      } /* End of loop over receivers */
      dprintf("After loops, thisScanWasGood = %d\n", thisScanWasGood);
      {
        /* long scansRemaining; */

	/*
        dSMStatus = dsm_read("hal9000", "DSM_AS_SCANS_REMAINING_L", (char *)&scansRemaining,
                             &timestamp);
        if (dSMStatus != DSM_SUCCESS)
          dsm_error_message(dSMStatus, "dsm_read (scans remaining)");
        if (thisScanWasGood && (scansRemaining > 0))
          scansRemaining--;
        dSMStatus = dsm_write_notify("M5ANDMONITOR", "DSM_AS_SCANS_REMAINING_L", (char *)&scansRemaining);
        if (dSMStatus != DSM_SUCCESS)
          dsm_error_message(dSMStatus, "dsm_write_notify (scans remaining)");
	*/
      }
      /*
	Keto Komment:
	For mosaicing, we could have offsets in RA and Dec. These need to
	be the same for all the antennas,  and we want one az and el to be 
	stored in the inh header. For the moment take it off the selected
	antenna.
      */
  
      az = scanCopy.header.antavg[lowestAntennaNumber].actual_az;
      el = scanCopy.header.antavg[lowestAntennaNumber].actual_el;
  
      /*
	Keto Komment:
	
	This section writes the integration header. If there is a number
	it usually means that we cannot get this data from the SMA now. 
	Most of these should be corrected at some future time.
      */
      
      /* Keto Komment: integration header initialization */

      inh.traid     = scanCopy.header.antavg[lowestAntennaNumber].project_id; /* track id # */
      inh.inhid     = globalScanNumber; /* integration id #          */
      inh.ints      = globalScanNumber; /* integration               */
      inh.az        = az;               /* azimuth                   */
      inh.el        = el;               /* elevation                 */
      inh.ha        = hAMidpoint;       /* hour angle                */
      inh.iut       = globalScanNumber; /* ut int code               */
      inh.iref_time = iRefTime;         /* ref_time int code         */
      inh.dhrs      = averageTime;      /* hrs from ref_time         */
      inh.vc        = (scanCopy.header.loData.vRadial - scanCopy.header.loData.vCatalog)/1.e3; /* vcorr for vctype          */
      inh.sx        = cos(decr)*cos(hAMidpoint * 15.0 * DEGREES_TO_RADIANS);  /* x vec. for bsl.           */
      inh.sy        = -cos(decr)*sin(hAMidpoint * 15.0 * DEGREES_TO_RADIANS); /* y vec. for bsl.           */
      inh.sz        = sin(decr);                                              /* z vec. for bsl.           */
      inh.rinteg    = scanCopy.data[lowestCrateNumber]->intTime; /* actual int time */
      inh.proid     = inh.traid;        /* project id #              */
      inh.souid     = thisSourceId;     /* source id #               */
      inh.isource   = thisSourceId;     /* source int code           */
      inh.ivrad     = thisVRadId;       /* position int code         */
      inh.offx      = scanCopy.header.antavg[lowestAntennaNumber].delta_ra;  /* offset in x */
      inh.offy      = scanCopy.header.antavg[lowestAntennaNumber].delta_dec; /* offset in y */
      inh.ira       = ira;                          /* ra int code               */
      inh.idec      = idec;                         /* dec int code              */
      inh.rar       = rar;                          /* ra (radians)              */
      inh.decr      = decr;                         /* declination (radians)     */
      inh.epoch     = 2000;                         /* epoch for coord.          */
      inh.size      = 0.0;
      inh.spareint1 = inh.spareint2 = inh.spareint3 = 
	inh.spareint4 = inh.spareint5 = inh.spareint6 = 0; /* Spare ints for later expansion */
      inh.sparedbl1 = inh.sparedbl2 = inh.sparedbl3 = 
	inh.sparedbl4 = inh.sparedbl5 = inh.sparedbl6 = 0.0; /* Spare doubles for later expansion */
      obsType = scanCopy.header.antavg[lowestAntennaNumber].obstype;
	dprintf("obstype = 0x%x\n", obsType);
      if ((obsType & SRC_TYPE_PLANET) ||
	  (scanCopy.header.antavg[lowestAntennaNumber].sol_sys_flag != 0)) {
	inh.size    = scanCopy.header.antavg[lowestAntennaNumber].planet_size;
	dprintf("Setting source size to %f\n", inh.size);
      }                                    /* source size               */
      if (store)
	fwrite_unlocked(&inh, sizeof(inhDef), 1, inFile);

      dprintf("Number of receivers: %d Number of sidebands: %d  numberOfBaselines: %d   averageTime %f\n",
	      numberOfReceivers, numberOfSidebands, numberOfBaselines, averageTime);
      if (store)
	schWrite(&sch, schFile);
      free(sch.packdata);
      if (doDSMWrite) {
#ifdef dadadaadada
	int dSMStatus, ii, jj, kk;
	long longAverageTime;
	float dSMAmp[11][11][2];    /* Array sizes hardcoded here, because          */
	float dSMPhase[11][11][2];  /* the DSM variables have hardcoded sizes       */
	float dSMCoh[11][11][2];    /* and we must math them even if the            */
	float dSMAmpH[11][11][2];   /* Array sizes hardcoded here, because          */
	float dSMPhaseH[11][11][2]; /* the DSM variables have hardcoded sizes       */
	float dSMCohH[11][11][2];   /* and we must math them even if the            */
        int dSMAntStatus[11];       /* value elsewhere in the code changes someday. */
#endif
        /* float dSMTsys[11][2]; */

	/*
	if (pathNameChanged) {
	  dSMStatus = dsm_write("HALANDMONITOR", "DSM_AS_FILE_NAME_C80", (char *)pathName);
	  if (dSMStatus != DSM_SUCCESS) {
	    dsm_error_message(dSMStatus, "dsm_write");
	    perror("writer: dsm write of file name to Hal failed");
	  }
	  pathNameChanged = FALSE;
	}
	for (ii = 0; ii < MAX_ANT+1; ii++) {
	  dSMTsys[ii][0] = scanCopy.header.antavg[ii].tsys;
	  dSMTsys[ii][1] = scanCopy.header.antavg[ii].tsys_rx2;
	  dSMAntStatus[ii] = scanCopy.header.antavg[ii].isvalid[0];
	  for (jj = 0; jj < MAX_ANT+1; jj++)
	    for (kk = 0; kk < MAX_SB; kk++) {
	      if (fullPolarization) {
		dSMAmp[ii][jj][kk] = pCAmp[0][ii][jj][kk][1];
		dSMPhase[ii][jj][kk] = pCPhase[0][ii][jj][kk][1];
		dSMCoh[ii][jj][kk] = pCCoh[0][ii][jj][kk][1];
		dSMAmpH[ii][jj][kk] = pCAmp[0][ii][jj][kk][1];
		dSMPhaseH[ii][jj][kk] = pCPhase[0][ii][jj][kk][1];
		dSMCohH[ii][jj][kk] = pCCoh[0][ii][jj][kk][1];
	      }
	      else if (doubleBandwidth) {
		dSMAmp[ii][jj][kk] = pCAmp[doubleBandwidthRx][ii][jj][kk][0];
		dSMPhase[ii][jj][kk] = pCPhase[doubleBandwidthRx][ii][jj][kk][0];
		dSMCoh[ii][jj][kk] = pCCoh[doubleBandwidthRx][ii][jj][kk][0];
		dSMAmpH[ii][jj][kk] = pCAmp[doubleBandwidthRx][ii][jj][kk][0];
		dSMPhaseH[ii][jj][kk] = pCPhase[doubleBandwidthRx][ii][jj][kk][0];
		dSMCohH[ii][jj][kk] = pCCoh[doubleBandwidthRx][ii][jj][kk][0];
	      } else {
		dSMAmp[ii][jj][kk] = pCAmp[0][ii][jj][kk][0];
		dSMPhase[ii][jj][kk] = pCPhase[0][ii][jj][kk][0];
		dSMCoh[ii][jj][kk] = pCCoh[0][ii][jj][kk][0];
		dSMAmpH[ii][jj][kk] = pCAmp[1][ii][jj][kk][0];
		dSMPhaseH[ii][jj][kk] = pCPhase[1][ii][jj][kk][0];
		dSMCohH[ii][jj][kk] = pCCoh[1][ii][jj][kk][0];
	      }
	    }
	}
	dSMStatus = dsm_structure_set_element(&visibilityInfo, "AMP_V11_V11_V2_F", (char *)dSMAmp);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "AMP_V11_V11_V2_F");
	dSMStatus = dsm_structure_set_element(&visibilityInfo, "PHASE_V11_V11_V2_F", (char *)dSMPhase);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "PHASE_V11_V11_V2_F");
	dSMStatus = dsm_structure_set_element(&visibilityInfo, "CORR_V11_V11_V2_F", (char *)dSMCoh);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "CORR_V11_V11_V2_F");
	dSMStatus = dsm_structure_set_element(&visibilityInfo, "AMP_H_V11_V11_V2_F", (char *)dSMAmpH);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "AMP_H_V11_V11_V2_F");
	dSMStatus = dsm_structure_set_element(&visibilityInfo, "PHASE_H_V11_V11_V2_F", (char *)dSMPhaseH);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "PHASE_H_V11_V11_V2_F");
	dSMStatus = dsm_structure_set_element(&visibilityInfo, "CORR_H_V11_V11_V2_F", (char *)dSMCohH);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "CORR_H_V11_V11_V2_F");
	dSMStatus = dsm_structure_set_element(&visibilityInfo, "SCAN_TSYS_V11_V2_F", (char *)dSMTsys);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "SCAN_TSYS_V11_V2_F");
	dSMStatus = dsm_write("HALANDMONITOR", "LAST_SCAN_VISIBILITES_X", &visibilityInfo);
	if (dSMStatus != DSM_SUCCESS) dsm_error_message(dSMStatus, "dsm_write()");
	
	dSMStatus = dsm_write("HALANDMONITOR", "DSM_AS_SCAN_SOURCE_C24", currentSource);
	if (dSMStatus != DSM_SUCCESS) {
	  dsm_error_message(dSMStatus, "dsm_write");
	  perror("writer: dsm write phase to Hal failed");
	}
	dSMStatus = dsm_write("hal9000", "DSM_AS_ANT_STATUS_V11_L", (char *)dSMAntStatus);
	if (dSMStatus != DSM_SUCCESS) {
	  dsm_error_message(dSMStatus, "dsm_write");
	  perror("writer: dsm write ant status to Hal failed");
	}
	longAverageTime = (long)averageTime;
	dSMStatus = dsm_write("hal9000", "DSM_AS_SCAN_TIME_L", (char *)&longAverageTime);
	if (dSMStatus != DSM_SUCCESS) {
	  dsm_error_message(dSMStatus, "dsm_write");
	  perror("writer: dsm write time to Hal failed");
	}
	dSMStatus = dsm_write("hal9000", "DSM_AS_SCAN_CORNUM_L",
			      (char *)&(scanCopy.data[lowestCrateNumber]->scanNumber));
	if (dSMStatus != DSM_SUCCESS) {
	  dsm_error_message(dSMStatus, "dsm_write");
	  perror("writer: dsm write c num to Hal failed");
	}
	*/
      } /* End of if (doDSMWrite) */
      if (store)
	for (rx = 0; rx < MAX_RX; rx++)
	  if (receiverActive[rx] && (!((rx != doubleBandwidthRx) && doubleBandwidth)))
	    fflush_unlocked(plotFile[rx]);
      fflush_unlocked(baselineFile);
      fflush_unlocked(weFile);
      fflush_unlocked(tsysFile);
      fflush_unlocked(codesFile);
      fflush_unlocked(engFile);
      fflush_unlocked(inFile);
      fflush_unlocked(spFile);
      fflush_unlocked(schFile);
      writeAutoData(globalScanNumber);
    } else /* End of if (lowestAntennaNumber > 0) */
      fprintf(stderr, "writer: No active antennas in scan - will not write anything\n");
    tempScanNumber = globalScanNumber++;
    deleteScan(&scanCopy, FALSE);
    needWrite = FALSE;
    pthread_mutex_unlock(&writeScanMutex);
    clock_gettime(CLOCK_REALTIME, &stopTime);
    stopTimeDouble = ((double)stopTime.tv_sec) + ((double)stopTime.tv_nsec)*1.0e-9;
    thisTime = stopTimeDouble-startTimeDouble;
    if (thisTime > maxTime)
      maxTime = thisTime;
    if (thisTime < minTime)
      minTime = thisTime;
    timeSum += thisTime;
    nTimes++;
    dprintf("\t\t***** Finished writing %d - time %f seconds (%f, %f, %f)\n", tempScanNumber,
	   thisTime, timeSum/((double)nTimes), maxTime, minTime);fflush(stdout);
  } /* End of while (TRUE) */
} /* End of writer */

/*

  R E A D  C O N F I G  F I L E S

  readConfigFiles reads the files used to establish the state of the server.
  At this moment, the only configuration file is the one which specifies
  which chunks should be ignored when calculating the pseudo-continuum
  channel.
*/
void readConfigFiles(void)
{
  int rx, ant1, ant2, chunk, line;
  FILE *badChunks;

  for (rx = 0; rx < MAX_RX+1; rx++)
    for (ant1 = 0; ant1 < MAX_ANT+1; ant1++)
      for (ant2 = 0; ant2 < MAX_ANT+1; ant2++)
	for (chunk = 0; chunk < MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK + 1; chunk++)
	  goodChunk[rx][ant1][ant2][chunk] = TRUE;

  badChunks = fopen("/global/configFiles/badChunks.txt", "r");
  if (badChunks == NULL) {
    fprintf(stderr, "readConfigFiles: no badChunks file seen - all chunks will be considered good\n");
  } else {
    line = 1;
    while (!feof(badChunks)) {
      int nRead;
      
      nRead = fscanf(badChunks, "%d %d %d %d", &rx, &ant1, &ant2, &chunk);
      if (!feof(badChunks)) {
	if (nRead != 4) {
	  fprintf(stderr, "readConfigFiles: Wrong number of entries seen on line %d\n",
		  line);
	} else if (rx == 0 && ant1 > 0 && ant1 < MAX_ANT && ant2 == 0 && chunk == 0) {
	  if (doubleBandwidth)
	    for (ant2 = 1; ant2 < MAX_ANT+1; ant2++)
	      for (chunk = 1; chunk <= MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK; chunk++)
		goodChunk[1][ant1][ant2][chunk] =
		  goodChunk[1][ant2][ant1][chunk] = FALSE;
	} else if ((rx > MAX_RX) || (rx < 1))
	  fprintf(stderr, "readConfigFiles: Illegal receiver number (%d) on line %d\n",
		  rx, line);
	else if ((ant1 > MAX_ANT) || (ant1 < 1))
	  fprintf(stderr, "readConfigFiles: Illegal 1st antenna number (%d) on line %d\n",
		  ant1, line);
	else if ((ant2 > MAX_ANT) || (ant1 < 1))
	  fprintf(stderr, "readConfigFiles: Illegal 2nd antenna number (%d) on line %d\n",
		  ant2, line);
	else if ((chunk > (MAX_BLOCK*MAX_CHUNK + MAX_INTERIM_CHUNK)) || (chunk < 1))
	  fprintf(stderr, "readConfigFiles: Illegal chunk number (%d) on line %d\n",
		  chunk, line);
	else {
	  dprintf("readConfigFiles:\tSetting chunk %d on baseline %d-%d, receiver %d to bad\n",
		  chunk, ant1, ant2, rx);
	  goodChunk[rx][ant1][ant2][chunk] =
	    goodChunk[rx][ant2][ant1][chunk] = FALSE;
	}
      }
      line++;
    }
    fclose(badChunks);
  }
} /* End of readConfigFiles */

/*

  S I G N A L  H A N D L E R

 Catch and handle any signals we expect to see
*/
void signalHandler(int signum)
{

  if (signum == SIGHUP) {
    if (safeRestarts)
      exit(0); /* Do this until I know I can restart properly */
    initializeArrays();
    readConfigFiles();
    needNewDataFile = TRUE;
    fprintf(stderr, 
	    "dataCatcher received SIGHUP - will open new files\n");
  } else
    fprintf(stderr, "integrationServer: Received unexpected signal #%d\n",
	    signum);
} /* End of signalHandler */

void startThreads(void)
{
  static int firstCall = TRUE;

  if (firstCall) {
    int rx1, rx2, nRx, rCode;
    pthread_attr_t attr;
    struct sched_param fifo_param;
    struct sigaction action, oldAction;
    FILE *scratchFile;
    
    scratchFile = fopen("/global/projects/receiversInArray", "r");
    if (scratchFile == NULL) {
      perror("receiversInArray");
      exit(-1);
    }
    nRx = fscanf(scratchFile, "%d %d", &rx1, &rx2);
    fclose(scratchFile);
    if ((nRx < 1) || (nRx > 2)) {
      fprintf(stderr, "Illegal number of tokens (%d) in receiversInArray file\n", nRx);
      exit(-1);
    }
    receiverActive[rx1-1] = TRUE;
    if (nRx > 1)
      receiverActive[rx2-1] = TRUE;
    if (doubleBandwidth) {
      if (receiverActive[0])
	doubleBandwidthRx = 0;
      else
	doubleBandwidthRx = 1;
      receiverActive[0] = receiverActive[1] = TRUE;
    }
    if (fullPolarization) {
      /*
	In full polarization, for the purposes of storing data, we will pretend
	that only the 345 GHz receiver is active.
      */
      receiverActive[0] = TRUE;
      receiverActive[1] = FALSE;
    }
    /* Bump up the priority of SERVER thread: */
    rCode = setpriority(PRIO_PROCESS, 0, SERVER_PRIORITY);
    if (rCode == -1) {
      perror("Bumping SERVER priority");
      exit(ERROR);
    }

    /*
    dSMStatus = dsm_open();
    if (dSMStatus != DSM_SUCCESS) {
      dsm_error_message(dSMStatus, "dsm_open");
      perror("catch_visibilities_1: dsm open failed");
    }
    */

    readConfigFiles();
    
    /*   C R E A T E   T H R E A D S   */

    if (pthread_attr_init(&attr) == ERROR) {
      perror("catch_visibilities_1: pthread_attr_init attr");
      exit(ERROR);
    }
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
 
    /*   H E A D E R   T H R E A D   */
    fifo_param.sched_priority = HEADER_PRIORITY;
    pthread_attr_setschedparam(&attr, &fifo_param);
    if (pthread_create(&headerTId, &attr, header,
		       (void *) 12) == ERROR) {
      perror("catch_visibilities_1: pthread_create header");
      fprintf(stderr, "thread create failure\n");
    }
    
    /*   W R I T E R   T H R E A D   */
    fifo_param.sched_priority = WRITER_PRIORITY;
    pthread_attr_setschedparam(&attr, &fifo_param);
    if (pthread_create(&writerTId, &attr, writer,
		       (void *) 12) == ERROR) {
      perror("catch_visibilities_1: pthread_create writer");
      fprintf(stderr, "thread create failure\n");
    }
    
    /*   C O P I E R   T H R E A D   */
    fifo_param.sched_priority = COPIER_PRIORITY;
    pthread_attr_setschedparam(&attr, &fifo_param);
    if (pthread_create(&copierTId, &attr, copier,
                       (void *) 12) == ERROR) {
      perror("catch_visibilities_1: pthread_create copier");
      fprintf(stderr, "thread create failure\n");
    }

    /*   E S T A B L I S H   S I G N A L   H A N D L E R  */
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    action.sa_handler = signalHandler;
    sigaction(SIGHUP, &action, &oldAction);  
    
    firstCall = FALSE;
  }
}

/*
  The following is the main RPC service function.
  The first call launches the threads HEADER and WRITER.
*/
dStatusStructure *result;
dStatusStructure *catch_visibilities_1(dCrateUVBlock *bundle, CLIENT *cl)
{
  /* short restarting; */
  /* int rCode; */
  /* int dSMStatus; */
  static int firstCall = TRUE;
  /* time_t timestamp; */
  
  if (firstCall) {
    result = (dStatusStructure *)malloc(sizeof(*result));
    if (result == NULL) {
      perror("catch_visibilities_1: malloc of result structure");
      exit(ERROR);
    }
    firstCall = FALSE;
  }
  startThreads();
  dprintf("catch_visibilities_1:\tGot a bundle from crate %d, time: %f, string: %s\n",
	  bundle->crateNumber, bundle->UTCtime,
	  bundle->sourceName); fflush(stdout);
  result->rt_code = OK;
  /*
  dSMStatus = dsm_read("hal9000", "DSM_AS_CORRELATOR_RESTARTING_S",
		       (char *)&restarting, &timestamp);
  if (dSMStatus != DSM_SUCCESS)
    perror("dsm read of DSM_AS_CORRELATOR_RESTARTING_S");
  if (restarting) {
    dprintf("Setting sendOperatorMessages to FALSE\n");
    sendOperatorMessages = FALSE;
  }
  */
  fflush(stdout);
  processBundle(bundle);
  return(result);
} /* End of catch_visibilities_1 */

/*
  C A T C H _ V I S I B I L I T I E S _ 1 _ S V C

  This routine is just a wrapper for catch_visibilities_1()
*/
dStatusStructure *catch_visibilities_1_svc(dCrateUVBlock *block, struct svc_req *dummy)
{
  CLIENT *cl = NULL;

    result = catch_visibilities_1(block, cl);
    return result;
} /* End of catch_visibilities_1_svc */

dStatusStructure *result2, *result3;

/*

  C A T C H _ P O W E R S _ 1

  The following routine receives the C2DC power level data from the
  correlator crates.

 */
dStatusStructure *catch_powers_1(dPowerSet *cratePower, CLIENT *cl)
{
  static int firstCall = TRUE;
  static int headerWritten = FALSE;
  int nAdjustments[3][9];
  float power[9][2][5], controlVoltages[3][9];
  char fileName[100];
  /* static dsm_structure c1DCStructure; */
  /* time_t timestamp; */
  FILE *powerLog;

  if (firstCall) {
    result2 = (dStatusStructure *)malloc(sizeof(*result2));
    if (result2 == NULL) {
      perror("malloc of result2");
      exit(-1);
    }
    /*
    dSMStatus = dsm_structure_init(&c1DCStructure, "C1DC_STATUS_X");
    if (dSMStatus != DSM_SUCCESS)
      perror("dsm_structure_init(&c1DCStructure, C1DC_STATUS_X)");
    */
    firstCall = FALSE;
  }
  if (haveLOData) {
    if (!headerWritten) {
      sprintf(fileName,"%s/c2DCPowerDetectors.txt", pathName);
      powerLog = fopen(fileName, "a");
      if (powerLog == NULL) {
	perror("fopen on powerLog");
      } else {
	int i, j, k, m;
	
	fprintf(powerLog, "# Chunk Center Frequencies: ");
	for (i = 0; i < 2; i++)
	  for (j = 0; j < 2; j++)
	    for (k = 0; k < 6; k++)
	      for (m = 0; m < 4; m++)
		fprintf(powerLog, "Rx%dSB%dBl%dCh%d %f ",
			i+1, j, k+1, m+1,
			globalFrequencies.receiver[i].sideband[j].block[k].chunk[m].centerfreq*1.0e-9);
	fprintf(powerLog, "\n");
	fprintf(powerLog, "# ");
	for (i = 1; i < 9; i++)
	  for (j = 0; j < 2; j++) {
	    for (k = 1; k < 5; k++)
	      fprintf(powerLog, "Pwr%d-%d-%d ", i,j+1,k);
	    fprintf(powerLog, "nAdj%d-%d Att%d-%d ", i, j+1, i, j+1);
	  }
	fprintf(powerLog, "\n");
	fclose(powerLog);
      }
      headerWritten = TRUE;
    }
    /*
    dSMStatus = dsm_read("hal9000", "C1DC_STATUS_X", &c1DCStructure, &timestamp);
    if (dSMStatus != DSM_SUCCESS)
      perror("dsm_read(hal9000, C1DC_STATUS_X, &c1DCStructure, &timestamp)");
    dSMStatus = dsm_structure_get_element(&c1DCStructure,
					  "N_ADJUSTMENTS_V3_V9_L",
					  (char *)nAdjustments);
    if (dSMStatus != DSM_SUCCESS)
      perror("get_element N_ADJUSTMENTS");
    dSMStatus = dsm_structure_get_element(&c1DCStructure,
					  "IF_CNTR_VOLT_V3_V9_F",
					  (char *)controlVoltages);
    if (dSMStatus != DSM_SUCCESS)
      perror("get_element N_ADJUSTMENTS");
    */
    printf("Crate %d called catch_powers_1\n", cratePower->crate);
    bcopy((char *)&(cratePower->data[0]), (char *)&power[0][0][0], 9*2*5*sizeof(float));
    sprintf(fileName,"%s/c2DCPowerDetectors.txt", pathName);
    powerLog = fopen(fileName, "a");
    if (powerLog == NULL) {
      perror("fopen on powerLog");
    } else {
      int i, j, k;
      time_t now;
      struct tm *nowValues;
      
      time(&now);
      nowValues = gmtime(&now);
      fprintf(powerLog, "%02d:%02d:%02d %d %d ",
	      nowValues->tm_hour, nowValues->tm_min, nowValues->tm_sec,
	      globalScanNumber, cratePower->crate);
      for (i = 1; i < 9; i++)
	for (j = 0; j < 2; j++) {
	  for (k = 1; k < 5; k++)
	    fprintf(powerLog, "%7.4f ", power[i][j][k]);
	  fprintf(powerLog, "%6d %6.4f ", nAdjustments[j][i], controlVoltages[j][i]);
	}
      fprintf(powerLog, "\n");
      fclose(powerLog);
    }
  }
  result2->rt_code = OK;
  return(result2);
} /* End of catch_powers_1 */

/*

  C A T C H _ P O W E R S _ 1 _ S V C

  This routine is just a wrapper for catch_powers_1()

 */
dStatusStructure *catch_powers_1_svc(dPowerSet *powerSet, struct svc_req *dummy)
{
  CLIENT *cl = NULL;

  result2 = catch_powers_1(powerSet, cl);
  return result2;
} /* End of catch_powers_1_svc */

typedef struct sWARMChunk {
  int filled;
  float lSBReal[N_SWARM_CHUNK_POINTS];
  float lSBImag[N_SWARM_CHUNK_POINTS];
  float uSBReal[N_SWARM_CHUNK_POINTS];
  float uSBImag[N_SWARM_CHUNK_POINTS];
} sWARMChunk;

typedef struct sWARMSScan {
  double uT;
  double duration;
  sWARMChunk *data[9][9][2];
} sWARMScan;

void destroySWARMScan(sWARMScan *scan) {
  int a1, a2, ch;

  for (a1 = 1; a1 < 8; a1++)
    for (a2 = a1+1; a2 <= 8; a2++)
      for (ch = 0; ch < 2; ch++)
	if (scan->data[a1][a2][ch])
	  free(scan->data[a1][a2][ch]);
  free(scan);
}

dCrateUVBlock *sWARMBundle;

/*
  S W A R M  2  B U N D L E

   This routine makes a bundle structure with the format of data from the legacy correlator,
and sends it to processBundle().   At that point there should be nothing structually unique
about the SWARM correlator data.

 */
void sWARM2Bundle(sWARMScan *scan) {
  int a1, a2, set, ch, sb, i;
  int nBaselines = 0;
  static int firstCall = TRUE;
  dVisibilitySet *vis;

  printf("I'm in sWARM2Bundle\n");
  if (firstCall) {
    sWARMBundle = (dCrateUVBlock *)malloc(sizeof(*sWARMBundle));
    if (sWARMBundle == NULL) {
      perror("malloc of sWARMBundle");
      exit(ERROR);
    }
    firstCall = FALSE;
  }
  for (a1 = 1; a1 < 8; a1++)
    for (a2 = a1+1; a2 <= 8; a2++)
      if (scan->data[a1][a2][0])
	nBaselines++;
  sprintf(sWARMBundle->sourceName, "SWARM Data");
  sWARMBundle->crateNumber = SWARM_CRATE;
  sWARMBundle->blockNumber = SWARM_BLOCK;
  sWARMBundle->scanType    = 1;
  sWARMBundle->UTCtime     = scan->uT;
  sWARMBundle->intTime     = scan->duration;
  if (sWARMBundle->intTime <= 0.0)
    sWARMBundle->intTime = 29.6827667;
  sWARMBundle->set.set_len = 2*nBaselines;
  sWARMBundle->set.set_val = (dVisibilitySet *)malloc(sWARMBundle->set.set_len*sizeof(dVisibilitySet));
  if (sWARMBundle->set.set_val == NULL) {
    perror("malloc of SWARM sWARMBundle->set.set_val");
    exit(ERROR);
  }
  set = 0;
  for (a1 = 1; a1 < 8; a1++)
    for (a2 = a1+1; a2 <= 8; a2++)
      if (scan->data[a1][a2][0]) {
	for (ch = 0; ch < 2; ch++) {
	  vis = &sWARMBundle->set.set_val[set];
	  vis->nPoints = N_SWARM_CHUNK_POINTS;
	  vis->lags.channel.channel_len = 0;
	  vis->lags.channel.channel_val = NULL;
	  vis->chunkNumber = ch+1;
	  vis->rxBoardHalf = 1;
	  vis->antennaNumber[0] = 0;
	  vis->antennaNumber[1] = a1;
	  vis->antennaNumber[2] = a2;
	  vis->real.real_len = 2;
	  vis->real.real_val = (dVarArray *)malloc(2*sizeof(dVarArray));
	  if (vis->real.real_val == NULL) {
	    perror("vis->real.real_val");
	    exit(-1);
	  }
	  vis->imag.imag_len = 2;
	  vis->imag.imag_val = (dVarArray *)malloc(2*sizeof(dVarArray));
	  if (vis->imag.imag_val == NULL) {
	    perror("vis->imag.imag_val");
	    exit(-1);
	  }	  
	  for (sb = 0; sb < 2; sb++) {
	    vis->real.real_val[sb].channel.channel_len = N_SWARM_CHUNK_POINTS;
	    vis->real.real_val[sb].channel.channel_val = (float *)malloc(N_SWARM_CHUNK_POINTS*sizeof(float));
	    if (vis->real.real_val[sb].channel.channel_val == NULL) {
	      perror("vis->real.real_val[sb].channel.channel_val");
	      exit(-1);
	    }
	    vis->imag.imag_val[sb].channel.channel_len = N_SWARM_CHUNK_POINTS;
	    vis->imag.imag_val[sb].channel.channel_val = (float *)malloc(N_SWARM_CHUNK_POINTS*sizeof(float));
	    if (vis->imag.imag_val[sb].channel.channel_val == NULL) {
	      perror("vis->imag.imag_val[sb].channel.channel_val");
	      exit(-1);
	    }
	    /* Copy over the actual data */
	    for (i = 0; i < N_SWARM_CHUNK_POINTS; i++) {
	      if (sb == 0) {
		vis->real.real_val[sb].channel.channel_val[i] =
		  scan->data[vis->antennaNumber[1]][vis->antennaNumber[2]][ch]->lSBReal[i];
		vis->imag.imag_val[sb].channel.channel_val[i] =
		  scan->data[vis->antennaNumber[1]][vis->antennaNumber[2]][ch]->lSBImag[i];
	      } else {
		vis->real.real_val[sb].channel.channel_val[i] =
		  scan->data[vis->antennaNumber[1]][vis->antennaNumber[2]][ch]->uSBReal[i];
		vis->imag.imag_val[sb].channel.channel_val[i] =
		  scan->data[vis->antennaNumber[1]][vis->antennaNumber[2]][ch]->uSBImag[i];
	      }
	    }
	  }
	  set++;
	}
      }
  /* printBundleInfo(sWARMBundle); */
  printf("Calling processBundle(sWARMBundle)\n");
  processBundle(sWARMBundle);
  printf("Returned from processBundle(sWARMBundle)\n");

  /* Now clean up all the malloc'd storage, except what never changes.  */
  set = 0;
  for (a1 = 1; a1 < 8; a1++)
    for (a2 = a1+1; a2 <= 8; a2++)
      if (scan->data[a1][a2][0]) {
	for (ch = 0; ch < 2; ch++) {
	  vis = &sWARMBundle->set.set_val[set];
	  for (sb = 0; sb < 2; sb++) {
	    free(vis->real.real_val[sb].channel.channel_val);
	    free(vis->imag.imag_val[sb].channel.channel_val);
	  }
	  free(vis->real.real_val);
	  free(vis->imag.imag_val);
	  set++;
	}
      }
  free(sWARMBundle->set.set_val);
}

/*
  C A T C H _ S W A R M _ D A T A _ 1

  The following routine receives the data packages from the SWARM
  correlator.

 */
dStatusStructure *catch_swarm_data_1(dSWARMUVBlock *data, CLIENT *cl)
{
  int i, ant1, ant2, chunk, nChannels;
  int missingA1 = 0, missingA2 = 0, missingCh = 0;
  int dataStillMissing = FALSE;
  static int nANPattern[8];
  static int gotSomeNANs = FALSE;
  double uT, duration;
  static int firstCall = TRUE;
  static int shouldInit = TRUE;
  static sWARMScan *scan;

  startThreads();
  if (firstCall) {
    result3 = (dStatusStructure *)malloc(sizeof(*result3));
    if (result3 == NULL) {
      perror("malloc of result3");
      exit(-1);
    }
    firstCall = FALSE;
  }
  if (!antennaInArrayInitialized) {
    getAntennaList(&antennaInArray[0]);
    antennaInArrayInitialized = TRUE;
  }
  if (shouldInit) {
    scan = (sWARMScan *)malloc(sizeof(sWARMScan));
    if (scan == NULL) {
      perror("Malloc failed for SWARM scan\n");
      exit(ERROR);
    }
    for (ant1 = 1; ant1 < 8; ant1++)
      for (ant2 = ant1+1; ant2 <= 8; ant2++)
	for (chunk = 0; chunk < 2; chunk++) {
	  if (antennaInArray[ant1] && antennaInArray[ant2]) {
	    scan->data[ant1][ant2][chunk] = (sWARMChunk *)malloc(sizeof(sWARMChunk));
	    if (scan->data[ant1][ant2][chunk] == NULL) {
	      perror("Malloc failed for SWARM chunk\n");
	      exit(ERROR);
	    }
	    scan->data[ant1][ant2][chunk]->filled = FALSE;
	  } else
	    scan->data[ant1][ant2][chunk] = NULL;
	}
    scan->uT = -1.0;
    shouldInit = FALSE;
  }
  ant1       = data->ant1;
  ant2       = data->ant2;
  if (ant1 > ant2) {
    int swapMe;

    swapMe = ant1;
    ant1 = ant2;
    ant2 = swapMe;
  }
  chunk      = data->chunk;
  if (chunk < 0)
    chunk = 0;
  if (chunk > 1)
    chunk = 1;
  nChannels  = data->nChannels;
  uT         = data->uT;
  duration   = data->duration;
  printf("I got %d points from %d-%d:%d taken at UT %f over %f seconds\n", nChannels, ant1, ant2, chunk, uT, duration);
  if (antennaInArray[ant1] && antennaInArray[ant2]) {
    if (ant1 == ant2) {
      /* It's an autocorrelation */
      sWARMAutoRec *newRec;
      
      printf("Got an autocorrelation from antenna %d\n", ant1);
      {
	int i, j;
	char sWARMNANPatternString[9];
	
	/* Derive the NAN pattern */
	gotSomeNANs = FALSE;
	for (i = 0; i < 8; i++)
	  if (isnan(data->lSB[i*8])) {
	    nANPattern[i] = FALSE;
	    gotSomeNANs = TRUE;
	  } else
	    nANPattern[i] = TRUE;
	for (i = 0; i < 8; i++)
	  if (nANPattern[i])
	    sWARMNANPatternString[i] = 'D';
	  else
	    sWARMNANPatternString[i] = 'N';
	sWARMNANPatternString[8] = (char)0;
	printf("NAN pattern is %s\n", sWARMNANPatternString);
	if (gotSomeNANs) {
	  /* Now replace the NANs with the average of the good channels */
	  for (i = 0; i < 256; i++) {
	    int nGoodData = 0;
	    float goodDataSum = 0.0;
	    
	    for (j = 0; j < 64; j++)
	      if (nANPattern[j/8]) {
		goodDataSum += data->lSB[i*64 + j];
		nGoodData++;
	      }
	    goodDataSum /= (float)nGoodData;
	    for (j = 0; j < 64; j++)
	      if (!nANPattern[j/8])
		data->lSB[i*64 + j] = goodDataSum;
	  }
	}
      }
      newRec = (sWARMAutoRec *)malloc(sizeof(sWARMAutoRec));
      if (newRec == NULL) {
	perror("new SWARM autocorrelation record");
	result3->rt_code = ERROR;
	return(result3);
      }
      newRec->next = NULL;
      newRec->autoData.antenna = ant1;
      for (i = 0; i < N_SWARM_CHUNK_POINTS; i++)
	newRec->autoData.amp[chunk][i] = data->lSB[i];
      pthread_mutex_lock(&autoMutex);
      if (sWARMAutoRoot == NULL) {
	sWARMAutoRoot = newRec;
	newRec->last = NULL;
      } else {
	sWARMAutoRec *endRec;

	endRec = sWARMAutoRoot;
	while (endRec->next != NULL)
	  endRec = endRec->next;
	endRec->next = newRec;
	newRec->last = endRec;
      }
      pthread_mutex_unlock(&autoMutex);
    } else {
      dprintf("OK, I need this baseline's data\n");
      if (gotSomeNANs) {
	int i, j;

	/* Replace any NANs with the average of the nearby good values */
	/* printf("___ Doing NAN replacement %d %d %d %d %d %d %d %d\n", */
	/*        nANPattern[0],nANPattern[1],nANPattern[2],nANPattern[3],nANPattern[4],nANPattern[5],nANPattern[6],nANPattern[7] ); */
	
	for (i = 0; i < 256; i++) {
	  int nGoodData = 0;
	  float goodLSBReal, goodLSBImag, goodUSBReal, goodUSBImag;

	  goodLSBReal = goodLSBImag = goodUSBReal = goodUSBImag = 0.0;
	  for (j = 0; j < 64; j++)
	    if (nANPattern[j/8]) {
	      goodUSBReal += data->uSB[(i*64 + j)*2];
	      goodUSBImag += data->uSB[(i*64 + j)*2 + 1];
	      goodLSBReal += data->lSB[(i*64 + j)*2];
	      goodLSBImag += data->lSB[(i*64 + j)*2 + 1];
	      nGoodData++;
	    }
	  goodUSBReal /= (float)nGoodData;
	  goodUSBImag /= (float)nGoodData;
	  goodLSBReal /= (float)nGoodData;
	  goodLSBImag /= (float)nGoodData;
	  for (j = 0; j < 64; j++)
	    if (!nANPattern[j/8]) {
	      /* printf("\t___ ch %d, %d  %f %f %f %f\n", (i*64 + j)*2, (i*64 + j)*2 + 1, */
	      /* 	     goodUSBReal, goodUSBImag, goodLSBReal, goodLSBImag); */
	      data->uSB[(i*64 + j)*2]     = goodUSBReal;
	      data->uSB[(i*64 + j)*2 + 1] = goodUSBImag;
	      data->lSB[(i*64 + j)*2]     = goodLSBReal;
	      data->lSB[(i*64 + j)*2 + 1] = goodLSBImag;
	    }
	}
      }
      if (scan->uT == -1.0) {
	scan->uT = uT;
      } else {
	if (fabs(scan->uT - uT) > 1.0)
	  fprintf(stderr, "SWARM scan times differ by more than 1 second\n");
      }
      if (scan->data[ant1][ant2][chunk]->filled) {
	fprintf(stderr, "Duplicate SWARM scan received - will abort\n");
	result3->rt_code = ERROR;
	return(result3);
      }
      for (i = 0; i < N_SWARM_CHUNK_POINTS; i++) {
	scan->data[ant1][ant2][chunk]->lSBReal[i] = data->lSB[2*i];
	scan->data[ant1][ant2][chunk]->lSBImag[i] = data->lSB[2*i + 1];
	scan->data[ant1][ant2][chunk]->uSBReal[i] = data->uSB[2*i];
	scan->data[ant1][ant2][chunk]->uSBImag[i] = data->uSB[2*i + 1];
      }
      scan->data[ant1][ant2][chunk]->filled = TRUE;
    }
  } else
    printf("Discarding unneeded %d-%d baseline data\n", ant1, ant2);
  /* Now check to see if we have a complete SWARM scan yet */
  for (ant1 = 1; ant1 < 8; ant1++)
    for (ant2 = ant1+1; ant2 <= 8; ant2++)
      for (chunk = 0; chunk < 2; chunk++)
	if (scan->data[ant1][ant2][chunk])
	  if (!scan->data[ant1][ant2][chunk]->filled) {
	    if (!dataStillMissing) {
	      missingA1 = ant1;
	      missingA2 = ant2;
	      missingCh = chunk;
	      dataStillMissing = TRUE;
	    }
	  }
  if (dataStillMissing) {
    dprintf("We're still missing some SWARM data for this scan (%d-%d: %d)\n",
	   missingA1, missingA2, missingCh);
  } else {
    printf("w00t - I've got a complete SWARM scan to process\n");
    sWARM2Bundle(scan);
    destroySWARMScan(scan);
    shouldInit = TRUE;
  }
  result3->rt_code = OK;
  return(result3);
} /* End of catch_swarm_data_1 */

/*
  C A T C H _ S W A R M _ D A T A _ 1 _ S V C

  This routine is just a wrapper for catch_swarm_data_1() 

 */
dStatusStructure *catch_swarm_data_1_svc(dSWARMUVBlock *data, struct svc_req *dummy)
{
  CLIENT *cl = NULL;

  result3 = catch_swarm_data_1(data, cl);
  return result3;
} /* End of catch_swarm_data_1_svc */

