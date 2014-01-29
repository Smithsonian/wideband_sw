 /*
  corrPlotter.c
  Author: Raoul Taco Namgozaar-Mohavi Abu-Zalaam Machilvich

  This program plots the data produced by the SMA correlator.   It is meant to
be used to get a quick-look at the data in realtime, primarily for the purpose
of verifying that good data is being produced.   It works with a companion
program, corrSaver.  corrSaver is an RPC server which will accept data sent from
the correlator, and store it in shared memory.   corrPlotter reads the data
from shared memory, and plots it.

  This program uses the motif toolkit to build a GUI, and raw xlib calls
to do the plotting.   It is efficient, but the coding must be done at a very low
level.  

*/

#include <X11/Xlib.h>
#include <X11/Xthreads.h>
#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/PushB.h>
#include <Xm/XpmP.h>
#include <Xm/FileSB.h>
#include <Xm/PanedW.h>
#include <Xm/MainW.h>
#include <Xm/RowColumn.h>
#include <Xm/BulletinB.h>
#include <Xm/CascadeB.h>
#include <Xm/Separator.h>
#include <Xm/Label.h>
#include <Xm/ToggleB.h>
#include <Xm/Text.h>
#include <Xm/Separator.h>
#include <Xm/SelectioB.h>
#include <Xm/MessageB.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h> 
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>

#include "corrPlotter.h"
#include "chunkPlot.h"
#include "/usr/include/popt.h"
#include "/global/include/dsm.h"
#include "/global/include/astrophys.h"

#define EXIT_SUCCESS    0
#define EXIT_FAILURE    1
#define SYSTEM_FAILURE  -1
#define ERROR_RETURN -1
#define SUCCESS_RETURN 0
#define N_BLOCKS 6
#define N_CRATE_PAIRS (6)
#define MAX_BASELINES (50)
#define FLIP_CHANNELS 0
#define BIG_WINDOW 6000000	/*
				  The minimum number of pixels needed for the
				  display to switch into "big mode" - see
				  resizeCB.
				*/
#define BAD_N_LEVEL_LOW  .090
#define BAD_N_LEVEL_HIGH .250
#define BAD_1_LEVEL_LOW  .250
#define BAD_1_LEVEL_HIGH .410

#define POL_SHOW_00 (1)
#define POL_SHOW_RR (2)
#define POL_SHOW_LL (4)
#define POL_SHOW_RL (8)
#define POL_SHOW_LR (16)
#define POL_SHOW_VV (32)
#define POL_SHOW_HH (64)
#define POL_SHOW_VH (128)
#define POL_SHOW_HV (256)

int polMask = POL_SHOW_00 | POL_SHOW_RR | POL_SHOW_LL | POL_SHOW_RL | POL_SHOW_LR | POL_SHOW_VV | POL_SHOW_HH | POL_SHOW_VH | POL_SHOW_HV;

#define SWARM_FRACTION (12)
#define SWARM_OFFSET (5)

#define MINN(A,B)  ((A)>(B)?(B):(A))  /* Min macro definition */
#define dprintf if (debugMessagesOn) printf /* Print IFF debugging          */

#define  LOW_RX_CODE (1)
#define HIGH_RX_CODE (0)
#define SWARM_IF     (2)
#define DONT_CHANGE_RX (137) /* Flag for labels that should not change the activeRx variable */

#define LATITUDE (0.345997658) /* Pad 1 latitude in radians */
#define USE_XT_LOCKS (TRUE)
#define USE_X11_MUTEX (FALSE)

float badNLevelLow = BAD_N_LEVEL_LOW;
float badNLevelHigh = BAD_N_LEVEL_HIGH;
float bad1LevelLow = BAD_1_LEVEL_LOW;
float bad1LevelHigh = BAD_1_LEVEL_HIGH;
int fieldSize = 1;
pthread_t sleeperTId, timerTId;
pthread_attr_t sleeperAttr;
pthread_mutexattr_t xDisplayMutAttr;
pthread_mutex_t xDisplayMut, dataMut, labelMut, cellMut, mallocMut, trackMut;

#define DEFAULT_RIGHT_MARGIN 1
#define ZOOMED_RIGHT_MARGIN 5
#define ERROR_RIGHT_MARGIN 164
#define DEFAULT_BOTTOM_MARGIN 12
#define ZOOMED_BOTTOM_MARGIN 25
#define SMALL_BOTTOM_MARGIN 0
#define DEFAULT_LEFT_MARGIN 23
#define ZOOMED_LEFT_MARGIN 5
#define DEFAULT_TOP_MARGIN 35
#define SMALL_TOP_MARGIN 21
#define ZOOMED_TOP_MARGIN 44
#define SMALL_BLOCK_SKIP 0
#define DEFAULT_BLOCK_SKIP 2

#define ESCAPE (27)

int closureBaseAnt = 1;
int wrapColor = TRUE;
int leftMargin = DEFAULT_LEFT_MARGIN;
int rightMargin = DEFAULT_RIGHT_MARGIN;
int topMargin = SMALL_TOP_MARGIN;
int bottomMargin = DEFAULT_BOTTOM_MARGIN;
int blockSkip = SMALL_BLOCK_SKIP;
int charHeight;
int cellX0, cellNChannels;
float cellAmp[4096], cellPhase[4096], cellInc;
int chunkSkip = 1;
int baselineSkip = 2;
int XDepth;
int drawnOnce = FALSE;
int activeRx = LOW_RX_CODE;
int plotOneBlockOnly = FALSE;
int useFullCatalog = FALSE;
int startScan = -1;
int xStartScan;
int xStartScanNumber;
int endScan = -1;
int xEndScan;
int xEndScanNumber;
int xScanRange;
int yTickTop;
int negateBsln = FALSE;
int internalEvent = FALSE;
int gotLineInfo = FALSE;
int antennaInArray[11];
double startTime = -100.0;
double endTime = -100.0;

int shouldSayRedrawing = TRUE;
int redrawAbort = FALSE;
int inRedrawScreenTrack = FALSE;
int interscanPause = 0;
int showClosure = FALSE;
int havePlottedSomething = FALSE;
int newPoints = FALSE;
int userSelectedPointSize = 0;
int lockXCount = 0;
int helpScreenActive = FALSE;
int disableUpdates = FALSE;
int scanMode = TRUE;
int autoCorrMode = FALSE;
int corrSaverMachine = TRUE;
char trackDirectory[1000];
int haveTrackDirectory = FALSE;
int showRefresh = FALSE;
int trackFileVersion = -1;
int zoomed = FALSE;
int zoomedAnt;
int sWARMZoomed = FALSE;
int sWARMLogPlot = TRUE;
int sWARMLinePlot = TRUE;
int sWARMZoomedMax = 0, sWARMZoomedMin = 0;
char sWARMNANPatternString[9];
int userSelectedSWARMAverage = -1;
int requestedBaselines[11][11];
int savedRequestedBaselines[11][11];
int nBlocksRequested = N_BLOCKS;
int savedNBlocksRequested;
int requestedBlockList[N_IFS*N_BLOCKS + 1] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};
int savedRequestedBlockList[N_IFS*N_BLOCKS + 1];
int nChunks = 4;
int savedNChunks;
int chunkList[N_CHUNKS] = {0, 1, 2, 3};
int savedChunkList[N_CHUNKS];
int nSidebands = 2;
int savedNSidebands;
int sBList[2] = {0, 1};
int savedSBList[2];
char sourceFilter[80];
char savedSourceFilter[80];
int currentSource = -1;
int blackListedSource[500];
int selectedSourceType = -1;
int savedCurrentSource = -1;
char bslnFilter[80];
char blockFilter[80];
char chunkFilter[80];
char sBFilter[80];
char rxFilter[80];
char savedBslnFilter[80];
int savedActiveRx;
char savedBlockFilter[80];
char savedChunkFilter[80];
char savedSBFilter[80];
int resizing = FALSE;
int checkStatistics = TRUE;
int showAmp = TRUE;
int showCoh = FALSE;
int showPhase = TRUE;
int showLags = FALSE;
int autoscaleAmplitude = TRUE;
int autoscalePhase = FALSE;
int shouldPlotSWARM = TRUE;
int nSWARMChannelsToDisplay = N_SWARM_CHANNELS/4096;
int bandLabeling = TRUE;
int plotFromZero = FALSE;
int plotToOne = FALSE;
int logPlot = FALSE;
int scaleInMHz = FALSE;
int debugMessagesOn = FALSE;
int integrate = FALSE;
char integrateSource[100];
int showGood = TRUE;
int showBad = FALSE;
int bslnOrder = FALSE;
int timePlot = TRUE;
int hAPlot = FALSE;
int applySecantZ = FALSE;
int grid = FALSE;
int doubleBandwidth = FALSE;
int nIntegrations = 1;
int shouldPlotResize = TRUE;
int resizeEventSeen = FALSE;
int redrawPending = FALSE;
int drawCBCalledByTimer = FALSE;
int gotBslnLength = FALSE;
float bslnLength[9][9];

typedef struct bslnEntry {
  short ant1;
  short ant2;
  short flag;
  float length;
  float amp[2];
  float phase[2];
  float coh[2];
} bslnEntry;

typedef struct plotLine {
  int polar;
  short sourceNumber;
  short sourceType;
  float uTC;
  float hA;
  float el;
  short nBaselines;
  bslnEntry *bsln;
  struct plotLine *next;
} plotLine;

/* Special "blocks" which act as flags (for cells): */
#define TIME_LABEL (-137)
#define SWARM_BLOCK (12)

int timeAxisA;
float timeAxisM, timeAxisB;

/*
  The cell structures hold the pixel coordinates of the boxes surounding
  items (spectra and labels) which are plotted in the graphics area.
  This information is used when a mouse click occurs, to determine
  what item was being selected.
*/
typedef struct cell {
  short tlcx;
  short tlcy;
  short trcx;
  short trcy;
  short blcx;
  short blcy;
  short brcx;
  short brcy;
  short ant1;
  short ant2;
  short iEf;
  short block;
  short chunk;
  short sb;
  short source;
  struct cell *next;
} cell;

cell *cellRoot = NULL;

typedef struct label {
  short tlcx;
  short tlcy;
  short trcx;
  short trcy;
  short blcx;
  short blcy;
  short brcx;
  short brcy;
  short ant1, ant2;
  short iEf;
  short block;
  short chunk;
  short sb;
  short source;
  struct label *next;
} label;

typedef struct lineEntry {
  short serialNumber;
  short speciesSerialNumber;
  char *species;
  short hasNickName;
  char *nickName;
  char *fullName;
  double frequency;
  double error;
  struct lineEntry *last;
  struct lineEntry *next;
} lineEntry;

lineEntry *lineRoot = NULL;
int nLineEntries = 0;
int nNamedLines = 0;

typedef struct lineFreqKey {
  lineEntry *ptr;
} lineFreqKey;

lineFreqKey *fullFreqIndex, *shortFreqIndex;

double transitionFreq;
int transitionBand, transitionSB;
double transitionOffset;
char transitionName[100];

typedef struct markListEntry {
  char *name;
  double frequency;
  int band;
  int sB;
  double offset;
  struct markListEntry *last;
  struct markListEntry *next;
} markListEntry;

markListEntry *markListRoot = NULL;

extern void XtProcessLock(
    void
			  );

extern void XtProcessUnlock(
    void
			    );
int addLineEntry(char *line) {
  int nRead;
  char *nextToken, *lasts;
  static lineEntry *tail;
  lineEntry *newEntry;
  
  newEntry = (lineEntry *)malloc(sizeof(lineEntry));
  if (newEntry == NULL) {
    perror("malloc of new line entry");
    return(ERROR_RETURN);
  }
  newEntry->next = NULL;
  if (lineRoot == NULL) {
    lineRoot = newEntry;
    newEntry->last = NULL;
  } else {
    tail->next = newEntry;
    newEntry->last = tail;
  }
  tail = newEntry;
  
  nextToken = strtok_r(line, "|", &lasts);
  if (nextToken == NULL) {
    fprintf(stderr,
	    "Error finding first token in line catalog line\n\"%s\"\nWill not use the line catalog.\n",
	    line);
    return(ERROR_RETURN);
  } else {
    nRead = sscanf(nextToken, "%hd", &newEntry->serialNumber);
    if (nRead != 1) {
      fprintf(stderr,
	      "Error parsing first token in line catalog line\n\"%s\"\nWill not use the line catalog.\n",
	      line);
      return(ERROR_RETURN);
    }
  }
  nextToken = strtok_r(NULL, "|", &lasts);
  if (nextToken == NULL) {
    fprintf(stderr,
	    "Error finding second token in line catalog line\n\"%s\"\nWill not use the line catalog.\n",
	    line);
    return(ERROR_RETURN);
  } else {
    int i = 0;
    int tempStringLength = 0;
    char tempString[100];

    while ((nextToken[i] != (char)0) && (tempStringLength < 100) && (nextToken[i] != '_')) {
      if (isprint(nextToken[i]) && (nextToken[i] != ' '))
	tempString[tempStringLength++] = nextToken[i];
      i++;
    }
    tempString[tempStringLength] = (char)0;
    newEntry->species = malloc(tempStringLength+1);
    if (newEntry->species == NULL) {
      perror("species name malloc");
      return(ERROR_RETURN);
    } else
      strcpy(newEntry->species, tempString);
    nRead = sscanf(&nextToken[i+1], "%hd", &newEntry->speciesSerialNumber);
    if (nRead != 1) {
      fprintf(stderr,
	      "Error parsing second token (%s) in line catalog line\n\"%s\" i= %d\nWill not use the line catalog.\n",
	      &nextToken[i+1], line, i);
      return(ERROR_RETURN);
    }
  }
  nextToken = strtok_r(NULL, "|", &lasts);
  if (nextToken == NULL) {
    fprintf(stderr,
	    "Error finding third token in line catalog line\n\"%s\"\nWill not use the line catalog.\n",
	    line);
    return(ERROR_RETURN);
  } else {
    int i = 0;
    int tempStringLength = 0;
    char tempString[100];
    
    while ((nextToken[i] != (char)0) && (tempStringLength < 100)) {
      if (isprint(nextToken[i]) && (nextToken[i] != ' '))
        tempString[tempStringLength++] = nextToken[i];
      i++;
    }
    if (tempStringLength > 0) {
      tempString[tempStringLength] = (char)0;
      newEntry->nickName = malloc(tempStringLength+1);
      if (newEntry->nickName == NULL) {
	perror("nick name malloc");
	return(ERROR_RETURN);
      } else
	strcpy(newEntry->nickName, tempString);
      nNamedLines++;
      newEntry->hasNickName = TRUE;
    } else {
      newEntry->hasNickName = FALSE;
      newEntry->nickName = NULL;
    }
  }
  nextToken = strtok_r(NULL, "|", &lasts);
  if (nextToken == NULL) {
    fprintf(stderr,
	    "Error finding fourth token in line catalog line\n\"%s\"\nWill not use the line catalog.\n",
	    line);
    return(ERROR_RETURN);
  } else {
    int i = 0;
    int tempStringLength = 0;
    char tempString[100];
    
    while ((nextToken[i] != (char)0) && (tempStringLength < 100)) {
      if (isprint(nextToken[i]) && (nextToken[i] != ' '))
        tempString[tempStringLength++] = nextToken[i];
      i++;
    }
    if (tempStringLength > 0) {
      tempString[tempStringLength] = (char)0;
      newEntry->fullName = malloc(tempStringLength+1);
      if (newEntry->fullName == NULL) {
	perror("fullName malloc");
	return(ERROR_RETURN);
      } else
	strcpy(newEntry->fullName, tempString);
    } else {
      newEntry->fullName = NULL;
      fprintf(stderr, "line catalog entry\n\"%s\"\nhas no full name - aborting.\n",
	      line);
      return(ERROR_RETURN);
    }
  }
  nextToken = strtok_r(NULL, "|", &lasts);
  if (nextToken == NULL) {
    fprintf(stderr,
	    "Error finding fifth token in line catalog line\n\"%s\"\nWill not use the line catalog.\n",
	    line);
    return(ERROR_RETURN);
  } else {
    nRead = sscanf(nextToken, "%lf", &newEntry->frequency);
    if (nRead != 1) {
      fprintf(stderr, "Error parsing fifth token (%s) in line cat entry\n\"%s\"\n - aborting\n",
	      nextToken, line);
      return(ERROR_RETURN);
    }
  }
  nextToken = strtok_r(NULL, "|", &lasts);
  if (nextToken == NULL) {
    fprintf(stderr,
	    "Error finding sixth token in line catalog line\n\"%s\"\nWill not use the line catalog.\n",
	    line);
    return(ERROR_RETURN);
  } else {
    nRead = sscanf(nextToken, "%lf", &newEntry->error);
    if (nRead != 1) {
      fprintf(stderr, "Error parsing sixth token (%s) in line cat entry\n\"%s\"\n - aborting\n",
	      nextToken, line);
      return(ERROR_RETURN);
    }
  }
  nLineEntries++;
  return(SUCCESS_RETURN);
}

Drawable activeDrawable;

Pixmap icon, shapeMask;
#define corrPlotter_bitmap_width 49
#define corrPlotter_bitmap_height 47
static unsigned char corrPlotter_bitmap_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xbf, 0xff, 0xff,
   0xff, 0x00, 0xfe, 0xff, 0xbf, 0xff, 0xff, 0xff, 0x00, 0xfe, 0xff, 0xbb,
   0x7e, 0xfe, 0xff, 0x00, 0xfe, 0xff, 0xb1, 0x56, 0xfe, 0xff, 0x00, 0xfe,
   0xff, 0x11, 0x42, 0xfc, 0xff, 0x00, 0xfe, 0xff, 0x01, 0x02, 0xf8, 0xff,
   0x00, 0xfe, 0xf7, 0x01, 0x00, 0xf8, 0xff, 0x00, 0xfe, 0x86, 0x00, 0x00,
   0xa1, 0xff, 0x00, 0xfe, 0x00, 0x24, 0x34, 0x87, 0xff, 0x00, 0xfe, 0x20,
   0x74, 0xf4, 0x87, 0xff, 0x00, 0x7e, 0xe0, 0xfe, 0xf6, 0x87, 0xff, 0x00,
   0x7e, 0xea, 0xfe, 0xf6, 0x8f, 0xf7, 0x00, 0x3e, 0xfa, 0xff, 0xff, 0x7f,
   0xe5, 0x00, 0x3e, 0xfa, 0xff, 0xff, 0x7f, 0xe0, 0x00, 0x3e, 0xff, 0xff,
   0xff, 0x7f, 0xe0, 0x00, 0x9e, 0xff, 0xff, 0xff, 0xff, 0xe3, 0x00, 0x9e,
   0xff, 0xff, 0xff, 0xff, 0xd7, 0x00, 0xce, 0xff, 0xff, 0xff, 0xff, 0xd7,
   0x00, 0xce, 0xff, 0xff, 0xff, 0xff, 0xd7, 0x00, 0xee, 0xff, 0xff, 0xff,
   0xff, 0xd7, 0x00, 0xee, 0xff, 0xff, 0xff, 0xff, 0xdf, 0x00, 0xee, 0xff,
   0xff, 0xff, 0xff, 0xdf, 0x00, 0xee, 0xff, 0xff, 0xff, 0xff, 0xbf, 0x00,
   0xee, 0xff, 0xff, 0xff, 0xff, 0xbf, 0x00, 0xee, 0xff, 0xff, 0xff, 0xff,
   0xbf, 0x00, 0xf4, 0xff, 0xff, 0xff, 0xff, 0xbf, 0x00, 0xf6, 0xff, 0xff,
   0xff, 0xff, 0x3f, 0x00, 0xf6, 0xff, 0xff, 0xff, 0xff, 0x81, 0x00, 0xf6,
   0xff, 0xff, 0x7f, 0x42, 0x40, 0x00, 0xf6, 0xff, 0x17, 0x46, 0x24, 0xfd,
   0x00, 0xf6, 0x22, 0x28, 0xa0, 0xef, 0xff, 0x00, 0x36, 0x40, 0xf4, 0xff,
   0xff, 0xff, 0x00, 0x82, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xc0, 0xff,
   0xff, 0xff, 0xff, 0xff, 0x00, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
   0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfa, 0xff, 0xff, 0xff, 0xff,
   0xff, 0x00, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfa, 0xff, 0xff,
   0xff, 0xff, 0xff, 0x00, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xf8,
   0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff,
   0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfe, 0xff, 0xff, 0xff,
   0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff,
   0xff, 0xff, 0xff, 0xff, 0x00};

label *labelBase = NULL;

XtAppContext app_context;
Display		*myDisplay;
Window		myWindow;
int		myscreen;
int nGcs = 0;
#define N_COLORS 50
char *colorName[N_COLORS] = {"white",
			     "red",
			     "green",
			     "orange",
			     "yellow",
			     "cyan",
			     "magenta",
			     "blue",
			     "olive drab",
			     "pink",
			     "lemon chiffon",
			     "purple",
			     "pale green",
			     "plum",
			     "DodgerBlue2",
			     "gold",
			     "wheat",
			     "PaleTurquoise1",
			     "salmon",
			     "tan",
			     "yellowgreen",
			     "SlateBlue1",
			     "DarkSeaGreen1",
			     "PaleVioletRed1",
			     "RosyBrown2",
			     "HotPink2",
			     "royal blue",		       
			     "maroon",
			     "purple3",
			     "chartreuse",
			     "dark orange",
			     "PaleGreen3",
			     "tan2",
			     "dark violet",
			     "saddle brown",
			     "coral",
			     "lavender",
			     "aquamarine1",
			     "LawnGreen",
			     "tomato",
			     "LightPink",
			     "firebrick",
			     "turquoise1",
			     "LightSkyBlue1",
			     "honeydew1",
			     "cornsilk1",
			     "chocolate1",
			     "tan4",
			     "cyan4",
			     "CadetBlue4"
};
int gcArrayOffset = 0;
GC              gcArray[N_COLORS];
GC		mygc, blueGc, whiteGc, yellowGc, greyGc, darkGreyGc, darkGreenGc,
  redGc, greenGc, yellowBfGc, labelGc,
		blueBfGc, whiteBfGc, blackGc;
XColor		red, blue, green, yellow, white, black, grey, darkGrey, darkGreen;
XColor          colorArray[N_COLORS];
unsigned long	myforeground, mybackground;
Widget		drawing;
Widget		rootParent;
Widget          trackFileDialog;
Widget          sourceDialog;
Widget          bslnDialog;
Widget          blockDialog;
Widget		chunkDialog;
Widget          rangeDialog;
Widget          timeRangeDialog;
Widget          testDialog;
Widget		sBDialog;
Widget		chanDialog;
Widget          rxDialog;
Widget		pauseDialog;
Widget          pointDialog;
Widget          sWARMAveDialog;
Widget          sWARMLogDialog;
Widget          helpDialog;
Widget          helpScroll;
Widget          typeRadioBox = 0;
Widget          radioBoxContainer;
Widget          polRadioBox = 0;
Widget          polRadioBoxContainer;
Widget          allSourcesWidget, fluxSourcesWidget, gainSourcesWidget, bandpassSourcesWidget, scienceSourcesWidget;
Widget          polAllWidget, polRRLLWidget, polRRWidget, polLLWidget, polRLLRWidget, polRLWidget, polLRWidget,
  polVVHHWidget, polVVWidget, polHHWidget, polVHHVWidget, polVHWidget, polHVWidget;
Colormap	cmap;
XFontStruct	*smallFontStruc, *bigFontStruc;
int		displayWidth = 652;
int		displayHeight = 500;

Widget mainwindow;
Widget menubar;
Widget filemenu, rxmenu, filtermenu, optionsmenu, helpmenu, modemenu;
Widget currentTrackButton;
Widget trackButton;
Widget sourceButton;
Widget bslnButton;
Widget blockButton;
Widget chunkButton;
Widget sBButton;
Widget chanButton;
Widget typeButton;
Widget polButton;
Widget hiToggle;
Widget loToggle;
Widget rxButton;
Widget pauseButton;
Widget rangeButton;
Widget timeRangeButton;
Widget testButton;
Widget pointButton;
Widget sWARMAveButton;
Widget closureBaseAntButton;
Widget helpButton;
Widget scanToggle;
Widget statsToggle;
Widget lagsToggle;
Widget cohToggle;
Widget ampToggle;
Widget negBslnToggle;
Widget phaseToggle;
Widget sWARMToggle;
Widget autoscaleToggle;
Widget autoscalePhaseToggle;
Widget sWARMLogToggle;
Widget sWARMLineToggle;
Widget bandLabelingToggle;
Widget plotFromZeroToggle;
Widget useFullCatalogToggle;
Widget plotToOneToggle;
Widget logPlotToggle;
Widget scaleInMHzToggle;
Widget integrateToggle;
Widget showGoodToggle;
Widget showBadToggle;
Widget showRefreshToggle;
Widget freezeToggle;
Widget closureToggle;
Widget bslnOrderToggle;
Widget wrapColorToggle;
Widget timePlotToggle;
Widget hAPlotToggle;
Widget applySecantZToggle;
Widget gridToggle;
Widget debugToggle;

Pixmap pixmap;

correlatorDef correlator, scratchCorrelatorCopy;

dsm_structure plotInfo;
int plotInfoInitialized = FALSE;

extern int getAntennaList(int *);

int chooseColor(int i)
{
  i += gcArrayOffset;
  while (i < 0)
    i += nGcs;
  if (wrapColor)
    return(i % nGcs);
  else
    return(MINN(i, nGcs-1));
}

#define SWAP(a,b) tempr=(a);(a)=(b);(b)=tempr

void four1(data,nn,isign)
float data[];
int nn,isign;
{
        int n,mmax,m,j,istep,i;
        double wtemp,wr,wpr,wpi,wi,theta;
        float tempr,tempi;

        n=nn << 1;
        j=1;
        for (i=1;i<n;i+=2) {
                if (j > i) {
                        SWAP(data[j],data[i]);
                        SWAP(data[j+1],data[i+1]);
                }
                m=n >> 1;
                while (m >= 2 && j > m) {
                        j -= m;
                        m >>= 1;
                }
                j += m;
        }
        mmax=2;
        while (n > mmax) {
                istep=2*mmax;
                theta=6.28318530717959/(isign*mmax);
                wtemp=sin(0.5*theta);
                wpr = -2.0*wtemp*wtemp;
                wpi=sin(theta);
                wr=1.0;
                wi=0.0;
                for (m=1;m<mmax;m+=2) {
                        for (i=m;i<=n;i+=istep) {
                                j=i+mmax;
                                tempr=wr*data[j]-wi*data[j+1];
                                tempi=wr*data[j+1]+wi*data[j];
                                data[j]=data[i]-tempr;
                                data[j+1]=data[i+1]-tempi;
                                data[i] += tempr;
                                data[i+1] += tempi;
                        }
                        wr=(wtemp=wr)*wpr-wi*wpi+wr;
                        wi=wi*wpr+wtemp*wpi+wi;
                }
                mmax=istep;
        }
}

#undef SWAP

#define ANT_R (1)
#define ANT_L (2)
#define ANT_V (3)
#define ANT_H (4)
int polarState(int ant1, int ant2, int polarInt)
{
  int s1, s2;

  s1 = (polarInt >> 3*ant1) & 0x7;
  s2 = (polarInt >> 3*ant2) & 0x7;
  if ((s1 == ANT_R) && (s2 == ANT_R))
    return(POL_SHOW_RR);
  else if ((s1 == ANT_R) && (s2 == ANT_L))
    return(POL_SHOW_RL);
  else if ((s1 == ANT_L) && (s2 == ANT_L))
    return(POL_SHOW_LL);
  else if ((s1 == ANT_L) && (s2 == ANT_R))
    return(POL_SHOW_LR);
  else if ((s1 == ANT_V) && (s2 == ANT_V))
    return(POL_SHOW_VV);
  else if ((s1 == ANT_V) && (s2 == ANT_H))
    return(POL_SHOW_VH);
  else if ((s1 == ANT_H) && (s2 == ANT_H))
    return(POL_SHOW_HH);
  else if ((s1 == ANT_H) && (s2 == ANT_V))
    return(POL_SHOW_HV);
  else
    return(POL_SHOW_00);
}

int plotSource(int source)
{
  if (source < 0)
    return(TRUE);
  else
    return(!blackListedSource[source]);
}

void getLine(FILE *theFile, char *line)
{
  int loopyDo = 0;
  int ptr = 0;
  int next = (int)'n';

  while ((!feof(theFile)) && (next != (int)'\n')) {
    next = getc(theFile);
    if ((!feof(theFile)) && (next != (int)'\n'))
      line[ptr++] = (char)next;
    if (loopyDo++ > 100000) {
      fprintf(stderr, "Excessive loop count in getLine - aboarting\n");
      exit(-1);
    }
  }
  line[ptr] = (char)0;
}

void lock_data(void)
{
  if (pthread_mutex_lock(&dataMut) == SYSTEM_FAILURE) {
    perror("lock_data");
    exit(SYSTEM_FAILURE);
  }
}

void unlock_data(void)
{
  if (pthread_mutex_unlock(&dataMut) == SYSTEM_FAILURE) {
    perror("unlock_data");
    exit(SYSTEM_FAILURE);
  }
}

void lock_label(char *caller)
{
  if (pthread_mutex_lock(&labelMut) == SYSTEM_FAILURE) {
    perror("lock_label");
    exit(SYSTEM_FAILURE);
  }
}

void unlock_label(char *caller)
{
  if (pthread_mutex_unlock(&labelMut) == SYSTEM_FAILURE) {
    perror("`unlock_label");
    exit(SYSTEM_FAILURE);
  }
}

void lock_cell(char *caller)
{
  if (pthread_mutex_lock(&cellMut) == SYSTEM_FAILURE) {
    perror("lock_cell");
    exit(SYSTEM_FAILURE);
  }
}

void unlock_cell(char *caller)
{
  if (pthread_mutex_unlock(&cellMut) == SYSTEM_FAILURE) {
    perror("unlock_cell");
    exit(SYSTEM_FAILURE);
  }
}

void lock_malloc(char *caller)
{
  if (pthread_mutex_lock(&mallocMut) == SYSTEM_FAILURE) {
    perror("lock_malloc");
    exit(SYSTEM_FAILURE);
  }
}

void unlock_malloc(char *caller)
{
  if (pthread_mutex_unlock(&mallocMut) == SYSTEM_FAILURE) {
    perror("unlock_malloc");
    exit(SYSTEM_FAILURE);
  }
}

void lock_track(char *caller)
{
  if (pthread_mutex_lock(&trackMut) == SYSTEM_FAILURE) {
    perror("lock_track");
    exit(SYSTEM_FAILURE);
  }
}

void unlock_track(char *caller)
{
  if (pthread_mutex_unlock(&trackMut) == SYSTEM_FAILURE) {
    perror("unlock_track");
    exit(SYSTEM_FAILURE);
  }
}

void lock_X_display()
     /*
       This routine locks the X11 display, in a (probably futile) attempt to share
       the display between independant threads.    This is definately unsound prac-
       tice - this program should be rewritten so that *ALL* X11 calls are contain-
       within a single thread.
     */
{
  if (debugMessagesOn)
    printf("lock_X called (lockCount = %d)\n", lockXCount);
  lockXCount++;
  if (USE_X11_MUTEX)
    if (pthread_mutex_lock(&xDisplayMut) == SYSTEM_FAILURE) {
      perror("lock_X_display");
      exit(SYSTEM_FAILURE);
    }
   if (USE_XT_LOCKS) {
    XtAppLock(app_context);
    XtProcessLock();
  }
}

void unlock_X_display(flush)

     /*
       This routine releases the X11 display for use by another thread.   If flush
       is TRUE, the X11 display queue is flushed before the lock is released.
     */
     
     int flush;
{
  if (debugMessagesOn)
    printf("unlock_X called, flush = %d (lockCount = %d)\n", flush, lockXCount);
  lockXCount--;
  if (flush) {
    XFlush(myDisplay);
    if (debugMessagesOn)
      printf("Done with flush\n");
  }
  if (USE_XT_LOCKS) {
    XtProcessUnlock();
    XtAppUnlock(app_context);
  }
  if (USE_X11_MUTEX)
    if (pthread_mutex_unlock(&xDisplayMut) == SYSTEM_FAILURE) {
      perror("unlock_X_display");
      exit(SYSTEM_FAILURE);
    }
  if (debugMessagesOn)
    printf("Exiting unlock_X\n");
}

/*
  This function returns the length of a string in pixels
*/
int stringWidth(char *string)
{
  unsigned minChar;
  int count = 0;
  int i;

  if (fieldSize == 2) {
    minChar = bigFontStruc->min_char_or_byte2;
    for (i = 0; i < strlen(string); i++) {
      count += bigFontStruc->per_char[(int)string[i]-minChar].rbearing-
	bigFontStruc->per_char[(int)string[i]-minChar].lbearing;
    }
  } else {
    minChar = smallFontStruc->min_char_or_byte2;
    for (i = 0; i < strlen(string); i++) {
      count += smallFontStruc->per_char[(int)string[i]-minChar].rbearing-
	smallFontStruc->per_char[(int)string[i]-minChar].lbearing;
    }
  }
  return(count);
}

void getBslnLength(void)
{
  int ant1, ant2;
  float x[9], y[9], z[9];
  char fileName[1000];
  FILE *bslnFile;

  sprintf(fileName, "%s/antennas", trackDirectory);
  bslnFile = fopen(fileName, "r");
  if (bslnFile != NULL) {
    for (ant1 = 1; ant1 < 9; ant1++)
      fscanf(bslnFile, "%d %e %e %e", &ant2, &x[ant1], &y[ant1], &z[ant1]);
    fclose(bslnFile);
    for (ant1 = 1; ant1 < 9; ant1++)
      for (ant2 = ant1+1; ant2 < 9; ant2++) {
	bslnLength[ant1][ant2] = bslnLength[ant2][ant1]
	  = sqrt(pow(x[ant1]-x[ant2],2.0) + pow(y[ant1]-y[ant2],2.0) + pow(z[ant1]-z[ant2],2.0));
      }
    gotBslnLength = TRUE;
  } else
    perror(fileName);
}

void selectCurrentTrack()
{
  int i;
  int latest = 0;
  int nDirNames = 1;
  char fullFileName[1000];
  char lastFile[1000];
  static char mirDir[100];
  static char *dirNames[] = {"engineering"};
  DIR *dirPtr;
  struct stat statBuffer;
  struct dirent *nextEnt;

  for (i = 0; i < nDirNames; i++) {
    sprintf(mirDir, "/data/%s/mir_data/", dirNames[i]);
    dirPtr = opendir(mirDir);
    while ((nextEnt = readdir(dirPtr)) != NULL) {
      if (strstr(nextEnt->d_name, ".") == NULL) {
	sprintf(fullFileName, "%s%s", mirDir, nextEnt->d_name);
	stat(fullFileName, &statBuffer);
	if (debugMessagesOn)
	  printf("Dir looping (%s) (%d, %d)!\n", fullFileName,
		 latest, (int)statBuffer.st_mtime);
	if (statBuffer.st_mtime > latest) {
	  if (debugMessagesOn)
	    printf("This one's the youngest so far!\n");
	  strcpy(lastFile, fullFileName);
	  latest = statBuffer.st_mtime;
	}
      }
    }
    closedir(dirPtr);
  }
  if (debugMessagesOn)
    printf("The most recent file is %s\n",
	   lastFile);
  strcpy(trackDirectory, lastFile);
  haveTrackDirectory = TRUE;
}

void sayRedrawing(void)
{
  int nChars;
  char textLine[100];

  if (shouldSayRedrawing) {
    if ((!disableUpdates) && (!showRefresh)) {
      sprintf(textLine, "Redrawing the display");
      nChars = strlen(textLine);
      XFillRectangle(myDisplay, myWindow, blackGc,
		     displayWidth/2 - stringWidth(textLine)/2 - 10,
		     displayHeight/2 - charHeight/2 - 15,
		     stringWidth(textLine) + 38,
		     charHeight + 10
		     );
      XDrawRectangle(myDisplay, myWindow, greenGc,
		     displayWidth/2 - stringWidth(textLine)/2 - 10,
		     displayHeight/2 - charHeight/2 - 15,
		     stringWidth(textLine) + 38,
		     charHeight + 10
		     );
      XDrawImageString(myDisplay, myWindow, greenGc,
		       displayWidth/2 - stringWidth(textLine)/2,
		       displayHeight/2 - charHeight/2,
		       textLine, nChars);
      XFlush(myDisplay);
      shouldSayRedrawing = FALSE;
    }
  }
}

void sayNoData(int caller)
{
  int nChars;
  char *noData = "No Data Available!";

  if (showRefresh) {
    activeDrawable = myWindow;
    XClearWindow(myDisplay, myWindow);
  } else {
    activeDrawable = pixmap;
    XFillRectangle(myDisplay, pixmap, blackGc, 0, 0, displayWidth, displayHeight);
  }
  nChars = strlen(noData);
  XDrawImageString(myDisplay, activeDrawable, redGc,
		   displayWidth/2 - stringWidth(noData)/2,
		   displayHeight/2 - charHeight/2,
		   noData, nChars);
}

int bslnPlottable(int a1, int a2)
{
  char scratch[10];

  if (strstr(bslnFilter, "*-*"))
    return(TRUE);
  if ((bslnFilter[0] == '*') && (strlen(bslnFilter)))
    return(TRUE);
  sprintf(scratch, "%d-*", a1);
  if (strstr(bslnFilter, scratch))
    return(TRUE);
  sprintf(scratch, "%d-*", a2);
  if (strstr(bslnFilter, scratch))
    return(TRUE);
  sprintf(scratch, "%d-%d", a1, a2);
  if (strstr(bslnFilter, scratch))
    return(TRUE);
  sprintf(scratch, "%d-%d", a2, a1);
  if (strstr(bslnFilter, scratch))
    return(TRUE);
  return(FALSE);
}

/*
  This is the heart of the program.   redrawScreen paints the spectra into
  the graphics area.   This is done when new data are available to be
  plotted, and when part of the window is exposed.
*/
void redrawScreen()
{
  int smallBottom;
  int nBlocks = 0;
  int nTotalBlocks = 0;
  int nBslnsBlock[N_BLOCKS] = {0, 0, 0, 0, 0, 0};
  int nBaselines = 0;
  int legacyEnd = 0;
  int nChars, sorted, sAOBlockPtr, aSIAABlockPtr, crate;
  int block, chunk, bsln, sb, channel, nChannels, chunkOffset;

  /*
    crateList[n][m] contains the crate number (offset from 0, not 1 as usual, so 0 -> 11)
    for baseline m of block n.
  */
  int crateList[N_BLOCKS][MAX_BASELINES];

  int sAOCrateSeen[N_CRATE_PAIRS] = {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};
  int aSIAACrateSeen[N_CRATE_PAIRS] = {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};
  int useRed = FALSE;
  char filters[200];
  int firstCell = TRUE;
  int firstLabel = TRUE;
  int validSWARMDataAvailable = FALSE;
  int log2nSWARMPixels;
  int plotSWARMOnly = FALSE;
  float amplitude;
  cell *cellPtr;
  label *labelPtr;
  typedef struct bslnTable {
    int antenna[2];
    int original;
  } bslnTable;
  bslnTable sortedBslns[N_BLOCKS][MAX_BASELINES];

  if (helpScreenActive)
    return;
  getAntennaList(antennaInArray);
  sayRedrawing();
  if (autoCorrMode) {
    
    lock_cell("autoCorr 1");
    if (cellRoot != NULL) {
      cell *nextCell;
      
	nextCell = (cell *)cellRoot->next;
	free(cellRoot);
	while (nextCell != NULL) {
	  cellPtr = nextCell;
	  nextCell = (cell *)cellPtr->next;
	  free(cellPtr);
	}
	cellRoot = NULL;
    }
    unlock_cell("autoCorr 1");
    if (showRefresh) {
      activeDrawable = myWindow;
      XClearWindow(myDisplay, myWindow);
    } else {
      activeDrawable = pixmap;
      XFillRectangle(myDisplay, pixmap, blackGc, 0, 0, displayWidth, displayHeight);
    }
#define AUTO_BOTTOM_SKIP (13)
#define AUTO_TOP_SKIP (24)
#define AUTO_LEFT_SKIP (2)
#define AUTO_RIGHT_SKIP (2)
    if (zoomed) {
      int i, nPoints, maxChan, minChan;
      float xStep, ampScale, ampMax, ampMin, xFloat;
      char scratchString[100];
      XPoint data[N_SWARM_CHANNELS], pData[N_SWARM_CHANNELS];
      int pltCount = 0;
      int nANCount = 0;
      int minX, maxX;
      int shouldPlot[N_SWARM_CHANNELS];

      {
	int i, ant, nANPattern[8];
	
	/* Derive the NAN pattern */
	ant = 1;
	while (!correlator.sWARMAutocorrelation[ant].haveAutoData)
	  ant++;
	for (i = 0; i < 8; i++)
	  if (isnan(correlator.sWARMAutocorrelation[ant].amp[0][i*8]))
	    nANPattern[i] = FALSE;
	  else
	    nANPattern[i] = TRUE;
	for (i = 0; i < 8; i++)
	  if (nANPattern[i])
	    sWARMNANPatternString[i] = 'D';
	  else
	    sWARMNANPatternString[i] = 'N';
	sWARMNANPatternString[8] = (char)0;
      }
      if (sWARMZoomedMin == sWARMZoomedMax) {
	minX = 0;
	maxX = N_SWARM_CHANNELS-1;
      } else {
	minX = sWARMZoomedMin;
	maxX = sWARMZoomedMax;
      }
      if (sWARMLogPlot) {
	sprintf(scratchString, "SWARM Autocorrelation for antenna %d *** LOG PLOT ***", zoomedAnt);
	XDrawImageString(myDisplay, activeDrawable, whiteGc,
			 displayWidth/2 - 100, 10,
			 scratchString, strlen(scratchString));
      } else {
	sprintf(scratchString, "SWARM Autocorrelation for antenna %d", zoomedAnt);
	XDrawImageString(myDisplay, activeDrawable, whiteGc,
			 displayWidth/2 - 20, 10,
			 scratchString, strlen(scratchString));
      }
      nPoints = maxX - minX + 1;
      xStep = (float)(displayWidth-AUTO_LEFT_SKIP-AUTO_RIGHT_SKIP)/(float)nPoints;
      ampMax = -1.0e30;
      ampMin = 1.0e30;
      if (sWARMLogPlot) {
	float datum;

	for (i = minX; i < maxX; i++) {
	  if (i > 0) {
	    datum = correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i];
	    if (isnan(datum)) {
	      shouldPlot[i] = FALSE;
	      nANCount++;
	    } else {
	      shouldPlot[i] = TRUE;
	      if (datum <= 0.0)
		datum = 0.0;
	      else
		datum = log(datum);
	      if (-datum > ampMax) {
		ampMax = -datum;
		maxChan = i;
	      }
	      if (-datum < ampMin) {
		ampMin = -datum;
		minChan = i;
	      }
	    }
	  }
	}
      } else {
	for (i = minX; i < maxX; i++) {
	  if (i > 0) {
	    if (isnan(correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i])) {
	      shouldPlot[i] = FALSE;
	      nANCount++;
	    } else {
	      shouldPlot[i] = TRUE;
	      if (-correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i] > ampMax) {
	      ampMax = -correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i];
	      maxChan = i;
	      }
	      if (-correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i] < ampMin) {
		ampMin = -correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i];
		minChan = i;
	      }
	    }
	  }
	}
      }
      sprintf(scratchString, "Minimum %e at channel %d Maximum %e at channel %d", -ampMax, maxChan, -ampMin, minChan);
      XDrawImageString(myDisplay, activeDrawable, whiteGc,
		       10, 18,
		       scratchString, strlen(scratchString));
      XDrawRectangle(myDisplay, activeDrawable, blueGc, AUTO_LEFT_SKIP, AUTO_TOP_SKIP,
		     displayWidth-AUTO_LEFT_SKIP-AUTO_RIGHT_SKIP, displayHeight-AUTO_BOTTOM_SKIP-AUTO_TOP_SKIP);
      
      ampScale = (float)(displayHeight-AUTO_BOTTOM_SKIP-AUTO_TOP_SKIP - 2)/(ampMax-ampMin);
      for (i = minX; i < maxX; i++) {
	xFloat = (float)(i-minX)*xStep;
	data[i].x = AUTO_LEFT_SKIP+(int)(xFloat+0.5);
	if (shouldPlot[i])
	  pData[pltCount].x = AUTO_LEFT_SKIP+(int)(xFloat+0.5);
	if (sWARMLogPlot) {
	  float datum;

	  datum = correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i];
	  if (datum <= 0.0)
	    datum = 0.0;
	  else
	    datum = log(datum);
	  data[i].y = 1 + (int)(AUTO_TOP_SKIP + (-datum-ampMin)*ampScale);
	  if (shouldPlot[i])
	    pData[pltCount++].y = 1 + (int)(AUTO_TOP_SKIP + (-datum-ampMin)*ampScale);
	} else {
	  data[i].y = 1 + (int)(AUTO_TOP_SKIP + (-correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i]-ampMin)*ampScale);
	  if (shouldPlot[i])
	    pData[pltCount++].y = 1 + (int)(AUTO_TOP_SKIP + (-correlator.sWARMAutocorrelation[zoomedAnt].amp[0][i]-ampMin)*ampScale);
	}
	if (displayWidth > 500)
	  if (((i % 1000) == 0) && (i > 0)) {
	    XDrawLine(myDisplay, activeDrawable, darkGreyGc, data[i].x, AUTO_TOP_SKIP, data[i].x, displayHeight-AUTO_BOTTOM_SKIP);
	    XDrawLine(myDisplay, activeDrawable, blueGc, data[i].x, AUTO_TOP_SKIP, data[i].x, AUTO_TOP_SKIP+5);
	    XDrawLine(myDisplay, activeDrawable, blueGc, data[i].x, displayHeight-AUTO_BOTTOM_SKIP,
		      data[i].x, displayHeight-AUTO_BOTTOM_SKIP-5);
	    sprintf(scratchString, "%d", i);
	    XDrawImageString(myDisplay, activeDrawable, blueGc,
			     data[i].x - 11, displayHeight - 2,
			     scratchString, strlen(scratchString));
	  }
      }
      if (sWARMLinePlot)
	XDrawLines(myDisplay, activeDrawable, whiteGc, pData, pltCount, CoordModeOrigin);
      else
	XDrawPoints(myDisplay, activeDrawable, whiteGc, pData, pltCount, CoordModeOrigin);
      if (nANCount > 0) {
	sprintf(scratchString, "%d (%d%%) NANs (not plotted) NAN Pattern: %s", nANCount, (int)((((float)(100*nANCount))/((float)nPoints)) + 0.5), sWARMNANPatternString);
	XDrawImageString(myDisplay, activeDrawable, redGc,
			 10, 38,
			 scratchString, strlen(scratchString));
      }
    } else { /* Not zoomed */
      int i, ant, j, cellWidth, cellHeight, nChansToAverage, nANPattern[8];
      float ampAve[N_SWARM_CHANNELS];
      char scratchString[100];
      int nAutosHor, nAutosVer, nAntsInProject;
      XPoint data[N_SWARM_CHANNELS];

      ant = getAntennaList(antennaInArray);

      /* Derive the NAN pattern */
      if (ant > 0) {
	while (!correlator.sWARMAutocorrelation[ant].haveAutoData)
	  ant++;
	for (i = 0; i < 8; i++)
	  if (isnan(correlator.sWARMAutocorrelation[ant].amp[0][i*8]))
	    nANPattern[i] = FALSE;
	  else
	    nANPattern[i] = TRUE;
	for (i = 0; i < 8; i++)
	  if (nANPattern[i])
	    sWARMNANPatternString[i] = 'D';
	  else
	    sWARMNANPatternString[i] = 'N';
	sWARMNANPatternString[8] = (char)0;
      }
      nAntsInProject = 0;
      for (i = 1; i <= 8; i++)
	if (antennaInArray[i])
	  nAntsInProject++;
      switch (nAntsInProject) {
      case 1:
	nAutosHor = 1;
	nAutosVer = 1;
	break;
      case 2:
	nAutosHor = 1;
	nAutosVer = 2;
	break;
      case 3:
      case 4:
	nAutosHor = 2;
	nAutosVer = 2;
	break;
      case 5:
      case 6:
	nAutosHor = 3;
	nAutosVer = 2;
	break;
      default:
	nAutosHor = 4;
	nAutosVer = 2;
      }

      cellWidth = (displayWidth-AUTO_LEFT_SKIP-AUTO_RIGHT_SKIP)/nAutosHor;
      cellHeight = (displayHeight-AUTO_BOTTOM_SKIP-AUTO_TOP_SKIP)/nAutosVer;
      /*
      if (userSelectedSWARMAverage > 0)
	nChansToAverage = userSelectedSWARMAverage;
      else
	nChansToAverage = (N_SWARM_CHANNELS/((int)(pow(2.0, round((int)log(cellWidth) + 0.5)))))/16;
      if (nChansToAverage < 1)
	nChansToAverage = 1;
      else if (nChansToAverage > 4096)
	nChansToAverage = 4096;
      */
      nChansToAverage = 8;
      ant = 1;
      if (sWARMLogPlot) {
	sprintf(scratchString, "SWARM Autocorrelations *** LOG PLOT *** - %d channels averaged per plotted point", nChansToAverage);
	XDrawImageString(myDisplay, activeDrawable, whiteGc,
			 displayWidth/2 - 200, 10,
			 scratchString, strlen(scratchString));
      } else {
	sprintf(scratchString, "SWARM Autocorrelations - %d channels averaged per plotted point", nChansToAverage);
	XDrawImageString(myDisplay, activeDrawable, whiteGc,
			 displayWidth/2 - 120, 10,
			 scratchString, strlen(scratchString));
      }
      for (i = 0; i < nAutosHor; i++)
	for (j = 0; j < nAutosVer; j++) {
	  sprintf(scratchString, "Ant %d", ant);
	  XDrawImageString(myDisplay, activeDrawable, greenGc,
			   AUTO_LEFT_SKIP+i*cellWidth+5,
			   AUTO_TOP_SKIP+j*cellHeight+12,
			   scratchString, strlen(scratchString));
	  if (correlator.sWARMAutocorrelation[ant].haveAutoData) {
	    int k, m, nPoints, maxChan, minChan, nPlotted;
	    float ampMax, ampMin, ampScale, xStep, xFloat;

	    nPoints = N_SWARM_CHANNELS/nChansToAverage;
	    nPlotted = 0;
	    xStep = (float)cellWidth/(float)nPoints;
	    
	    if (cellWidth > 210) {
	      ampMax = -1.0e30;
	      ampMin = 1.0e30;
	      if (sWARMLogPlot) {
		float datum;

		for (k = 1; k < N_SWARM_CHANNELS; k++) {
		  datum = correlator.sWARMAutocorrelation[ant].amp[0][k];
		  if (datum <= 0.0)
		    datum = 0.0;
		  else
		    datum = log(datum);
		  if (datum > ampMax) {
		    ampMax = datum;
		    maxChan = k;
		  }
		  if (datum < ampMin) {
		    ampMin = datum;
		    minChan = k;
		  }
		}
	      } else {
		for (k = 1; k < N_SWARM_CHANNELS; k++) {
		  if (correlator.sWARMAutocorrelation[ant].amp[0][k] > ampMax) {
		    ampMax = correlator.sWARMAutocorrelation[ant].amp[0][k];
		    maxChan = k;
		  }
		  if (correlator.sWARMAutocorrelation[ant].amp[0][k] < ampMin) {
		    ampMin = correlator.sWARMAutocorrelation[ant].amp[0][k];
		    minChan = k;
		  }
		}
	      }
	      sprintf(scratchString, "Max %6.1e at %d Min %6.1e at %d", ampMax, maxChan, ampMin, minChan);
	      if (j == 0)
		XDrawImageString(myDisplay, activeDrawable, whiteGc,
				 AUTO_LEFT_SKIP+i*cellWidth + cellWidth/2 - 97,
				 AUTO_TOP_SKIP+j*cellHeight - 1,
				 scratchString, strlen(scratchString));
	      else
		XDrawImageString(myDisplay, activeDrawable, whiteGc,
				 AUTO_LEFT_SKIP+i*cellWidth + cellWidth/2 - 97,
				 AUTO_TOP_SKIP+(j+1)*cellHeight + 11,
				 scratchString, strlen(scratchString));
	    }
	    XDrawRectangle(myDisplay, activeDrawable, blueGc, AUTO_LEFT_SKIP+i*cellWidth, AUTO_TOP_SKIP+j*cellHeight,
			   cellWidth, cellHeight);
	    for (k = 0; k < nPoints; k++) {
	      int count;

	      ampAve[k] = 0.0;
	      count = 0;
	      for (m = 0; m < nChansToAverage; m++)
		if ((k*nChansToAverage + m) >= 0) {
		  if ((k*nChansToAverage + m) > 0) {
		    if (sWARMLogPlot) {
		      float datum;

		      datum = correlator.sWARMAutocorrelation[ant].amp[0][k*nChansToAverage + m];
		      if (datum <= 0.0)
			datum = 0.0;
		      else
			datum = log(datum);
		      ampAve[k] -= datum;
		    } else
		      ampAve[k] -= correlator.sWARMAutocorrelation[ant].amp[0][k*nChansToAverage + m];
		    count++;
		  }
		}
	      if (count > 0)
		ampAve[k] /= (float)count;
	    }
	    ampMax = -1.0e30;
	    ampMin = 1.0e30;
	    for (k = 0; k < nPoints; k++) {
	      if (ampAve[k] > ampMax)
		ampMax = ampAve[k];
	      if (ampAve[k] < ampMin)
		ampMin = ampAve[k];
	    }
	    ampScale = (float)(cellHeight-2)/(ampMax-ampMin);
	    for (k = 0; k < nPoints; k++) {
	      if (nANPattern[k % 8]) {
		xFloat = (float)k*xStep;
		data[nPlotted].x = AUTO_LEFT_SKIP+i*cellWidth+(int)(xFloat+0.5);
		data[nPlotted++].y = 1 + (int)(AUTO_TOP_SKIP+j*cellHeight + (ampAve[k]-ampMin)*ampScale);
	      }
	    }
	    if (sWARMLinePlot)
	      XDrawLines(myDisplay, activeDrawable, whiteGc, data, nPlotted, CoordModeOrigin);
	    else
	      XDrawPoints(myDisplay, activeDrawable, whiteGc, data, nPlotted, CoordModeOrigin);
	    lock_cell("autoCorr 2");
	    if (firstCell) {
	      lock_malloc(NULL);
	      cellRoot = (cell *)malloc(sizeof(cell));
	      unlock_malloc(NULL);
	      cellRoot->next = NULL;
	      firstCell = FALSE;
	      cellPtr = cellRoot;
	    } else {
	      cell *nextCell;
	      
	      nextCell = cellRoot;
	      while (nextCell != NULL) {
		cellPtr = nextCell;
		nextCell = (cell *)cellPtr->next;
	      }
	      lock_malloc(NULL);
	      cellPtr->next = (cell *)malloc(sizeof(cell));
	      unlock_malloc(NULL);
	      cellPtr = cellPtr->next;
	      cellPtr->next = NULL;
	    }
	    cellPtr->tlcx = AUTO_LEFT_SKIP+i*cellWidth;
	    cellPtr->tlcy = AUTO_TOP_SKIP+j*cellHeight;
	    cellPtr->trcx = cellPtr->tlcx+cellWidth;
	    cellPtr->trcy = cellPtr->tlcy;
	    cellPtr->blcx = cellPtr->tlcx;
	    cellPtr->blcy = cellPtr->tlcy+cellHeight;
	    cellPtr->brcx = cellPtr->tlcx+cellWidth;
	    cellPtr->brcy = cellPtr->blcy;
	    cellPtr->ant1 = ant;
	    cellPtr->ant2 = ant;
	    unlock_cell("autoCorr 2");
	  } else { /* No autocorrelation data available for this antenna */
	    XDrawRectangle(myDisplay, activeDrawable, blueGc, AUTO_LEFT_SKIP+i*cellWidth, AUTO_TOP_SKIP+j*cellHeight,
			   cellWidth, cellHeight);
	    sprintf(scratchString, "No Data");
	    XDrawImageString(myDisplay, activeDrawable, greenGc,
			     AUTO_LEFT_SKIP+i*cellWidth+cellWidth/2-15,
			     AUTO_TOP_SKIP+j*cellHeight+cellHeight/2+2,
			     scratchString, strlen(scratchString));
	  }
	  ant++;
	}
    }
  } else { /* Not autoCorrMode */
    /* Check to see if any of the SWARM data is marked as good */
    if (shouldPlotSWARM && !(plotOneBlockOnly && !requestedBlockList[SWARM_BLOCK]))
      for (bsln = 0; bsln < N_BASELINES_PER_CRATE; bsln++)
	if (correlator.sWARMBaseline[bsln].haveCrossData)
	  validSWARMDataAvailable = TRUE;
    if (plotOneBlockOnly && requestedBlockList[SWARM_BLOCK])
      plotSWARMOnly = TRUE;
    bzero(sortedBslns, N_BLOCKS*MAX_BASELINES*sizeof(bslnTable));
    if (bslnOrder && (!gotBslnLength)) {
      selectCurrentTrack();
      getBslnLength();
    }
    if ((bandLabeling) && (!zoomed) && (!sWARMZoomed)) {
      topMargin = SMALL_TOP_MARGIN;
      blockSkip = SMALL_BLOCK_SKIP;
    } else {
      topMargin = DEFAULT_TOP_MARGIN;
      blockSkip = DEFAULT_BLOCK_SKIP;
    }
    if (debugMessagesOn)
      printf("In redrawScreen - get rid of old cell list if any (1)\n");
    lock_cell("1");
    if (cellRoot != NULL) {
      cell *nextCell;
      
      nextCell = (cell *)cellRoot->next;
      free(cellRoot);
      while (nextCell != NULL) {
	cellPtr = nextCell;
	nextCell = (cell *)cellPtr->next;
	free(cellPtr);
      }
      cellRoot = NULL;
    }
    unlock_cell("1");
    if (debugMessagesOn)
      printf("In redrawScreen - get rid of old label list if any\n");
    lock_label("1");
    if (labelBase != NULL) {
      label *nextLabel;
      
      nextLabel = (label *)labelBase->next;
      free(labelBase);
      while (nextLabel != NULL) {
	labelPtr = nextLabel;
	nextLabel = (label *)labelPtr->next;
	free(labelPtr);
      }
    }
    labelBase = NULL;
    unlock_label("1");
    if (fieldSize == 1)
      charHeight = bigFontStruc->max_bounds.ascent + bigFontStruc->max_bounds.descent;
    else
      charHeight = smallFontStruc->max_bounds.ascent + smallFontStruc->max_bounds.descent;
    charHeight -= 4;
    lock_X_display();
    dprintf("X11 locked\n");
    lock_data();
    dprintf("Data locked\n");
    bzero(crateList, N_BLOCKS*MAX_BASELINES*sizeof(int));
    for (crate = 0; crate < N_BLOCKS; crate++) {
      block = crate % N_BLOCKS;
      if (correlator.header.crateActive[crate] &&
	  requestedBlockList[block]) {
	for (bsln = 0; bsln < MAX_BASELINES; bsln++)
	  crateList[nBlocks][bsln] = block;
	if (crate <= N_BLOCKS)
	  nBlocks++;
      }
    }
    nTotalBlocks = nBlocks;
    if (doubleBandwidth && !zoomed && !sWARMZoomed && !plotOneBlockOnly)
      for (crate = 0; crate < N_BLOCKS; crate++) {
	block = (crate % N_BLOCKS) + 6;
	if (correlator.header.crateActive[crate] &&
	    requestedBlockList[block]) {
	  if (crate <= N_BLOCKS)
	    nTotalBlocks++;
	}
      }
    nBaselines = sAOBlockPtr = aSIAABlockPtr = 0;
    for (crate = 0; crate < N_CRATES; crate++) {
      int bslnPtr;
      
      block = crate % N_BLOCKS;
      dprintf("crate %d, block %d, active: %d, requested: %d\n",
	      crate, block,
	      correlator.header.crateActive[crate],
	      requestedBlockList[block]);
      /*
	O.K. - what's going on here?   We're looping through correlator crates, trying to develope a list
	of what crate will have which block on which baseline.
      */
      if (correlator.header.crateActive[crate] &&
	  requestedBlockList[block]) {
	if ((crate < N_BLOCKS) && (!sAOCrateSeen[block])) {
	  nBslnsBlock[block] = 0;
	  bslnPtr = 0;
	  dprintf("Starting SAO while loop (correlator.crate[%d].description.baselineInUse[%d][%d]) = %d)\n",
		  crate, activeRx, bslnPtr, correlator.crate[crate].description.baselineInUse[activeRx][bslnPtr]);
	  while ((correlator.crate[crate].description.baselineInUse[activeRx][bslnPtr]) &&
		 (bslnPtr < N_BASELINES_PER_CRATE)) {
	    dprintf("crate %d %d-%d: %d\n", crate, correlator.crate[crate].data[bslnPtr].antenna[0], correlator.crate[crate].data[bslnPtr].antenna[1],
		    requestedBaselines[correlator.crate[crate].data[bslnPtr].antenna[0]][correlator.crate[crate].data[bslnPtr].antenna[1]]);
	    if (requestedBaselines[correlator.crate[crate].data[bslnPtr].antenna[0]][correlator.crate[crate].data[bslnPtr].antenna[1]]) {
	      sortedBslns[sAOBlockPtr][nBslnsBlock[block]].antenna[0] =
		correlator.crate[crate].data[bslnPtr].antenna[0];
	      sortedBslns[sAOBlockPtr][nBslnsBlock[block]].antenna[1] =
		correlator.crate[crate].data[bslnPtr].antenna[1];
	      sortedBslns[sAOBlockPtr][nBslnsBlock[block]].original = bslnPtr;
	      nBslnsBlock[block]++;
	    }
	    bslnPtr++;
	  }
	  sAOBlockPtr++;
	  sAOCrateSeen[block] = TRUE;
	}
	dprintf("crate %d, ACS[%d] %d\n", crate, block, aSIAACrateSeen[block]);
	if ((crate > 5) && (!aSIAACrateSeen[block])) {
	  bslnPtr = 0;
	  dprintf("Starting ASIAA while loop (correlator.crate[%d].description.baselineInUse[%d][%d]) = %d)\n",
		  crate, activeRx, bslnPtr, correlator.crate[crate].description.baselineInUse[activeRx][bslnPtr]);
	  while (correlator.crate[crate].description.baselineInUse[activeRx][bslnPtr]) {
	    dprintf("A crate %d %d-%d: %d\n", crate, correlator.crate[crate].data[bslnPtr].antenna[0], correlator.crate[crate].data[bslnPtr].antenna[1],
		    requestedBaselines[correlator.crate[crate].data[bslnPtr].antenna[0]][correlator.crate[crate].data[bslnPtr].antenna[1]]);
	    if (requestedBaselines[correlator.crate[crate].data[bslnPtr].antenna[0]][correlator.crate[crate].data[bslnPtr].antenna[1]]) {
	      sortedBslns[aSIAABlockPtr][nBslnsBlock[block]].antenna[0] =
		correlator.crate[crate].data[bslnPtr].antenna[0];
	      sortedBslns[aSIAABlockPtr][nBslnsBlock[block]].antenna[1] =
		correlator.crate[crate].data[bslnPtr].antenna[1];
	      sortedBslns[aSIAABlockPtr][nBslnsBlock[block]].original = bslnPtr;
	      crateList[aSIAABlockPtr][nBslnsBlock[block]] += N_BLOCKS;
	      nBslnsBlock[block]++;
	    }
	    bslnPtr++;
	  }
	  aSIAABlockPtr++;
	  aSIAACrateSeen[block] = TRUE;
	}
      }
    }
    for (block = 0; block < N_BLOCKS; block++)
      if (nBslnsBlock[block] != 0) {
	nBaselines = nBslnsBlock[block];
	break;
      }
    dprintf("Before sort, nBaselines = %d\n", nBaselines);
    if (nBaselines > 1) {
      int tempBlockPtr;
      
      for (block = 0; block < nBlocks; block++) {
	/* Bubblesort! */
	sorted = FALSE;
	while (!sorted) {
	  bslnTable temp;
	  
	  sorted = TRUE;
	  for (bsln = 0; bsln < (nBaselines-1); bsln++) {
	    if ((!bslnOrder) || (!gotBslnLength)) {
	      if (sortedBslns[block][bsln].antenna[0] >
		  sortedBslns[block][bsln+1].antenna[0]) {
		sorted = FALSE;
		temp.antenna[0] = sortedBslns[block][bsln].antenna[0];
		temp.antenna[1] = sortedBslns[block][bsln].antenna[1];
		temp.original = sortedBslns[block][bsln].original;
		sortedBslns[block][bsln].antenna[0] = sortedBslns[block][bsln+1].antenna[0];
		sortedBslns[block][bsln].antenna[1] = sortedBslns[block][bsln+1].antenna[1];
		sortedBslns[block][bsln].original = sortedBslns[block][bsln+1].original;
		sortedBslns[block][bsln+1].antenna[0] = temp.antenna[0];
		sortedBslns[block][bsln+1].antenna[1] = temp.antenna[1];
		sortedBslns[block][bsln+1].original = temp.original;
		tempBlockPtr = crateList[block][bsln];
		crateList[block][bsln] = crateList[block][bsln+1];
		crateList[block][bsln+1] = tempBlockPtr;
	      } else if ((sortedBslns[block][bsln].antenna[0] ==
			  sortedBslns[block][bsln+1].antenna[0]) &&
			 (sortedBslns[block][bsln].antenna[1] >
			  sortedBslns[block][bsln+1].antenna[1])) {
		sorted = FALSE;
		temp.antenna[0] = sortedBslns[block][bsln].antenna[0];
		temp.antenna[1] = sortedBslns[block][bsln].antenna[1];
		temp.original = sortedBslns[block][bsln].original;
		sortedBslns[block][bsln].antenna[0] = sortedBslns[block][bsln+1].antenna[0];
		sortedBslns[block][bsln].antenna[1] = sortedBslns[block][bsln+1].antenna[1];
		sortedBslns[block][bsln].original = sortedBslns[block][bsln+1].original;
		sortedBslns[block][bsln+1].antenna[0] = temp.antenna[0];
		sortedBslns[block][bsln+1].antenna[1] = temp.antenna[1];
		sortedBslns[block][bsln+1].original = temp.original;
		tempBlockPtr = crateList[block][bsln];
		crateList[block][bsln] = crateList[block][bsln+1];
		crateList[block][bsln+1] = tempBlockPtr;
	      }
	    } else if (bslnLength[sortedBslns[block][bsln].antenna[0]][sortedBslns[block][bsln].antenna[1]]
		       > bslnLength[sortedBslns[block][bsln+1].antenna[0]][sortedBslns[block][bsln+1].antenna[1]]) {
	      sorted = FALSE;
	      temp.antenna[0] = sortedBslns[block][bsln].antenna[0];
	      temp.antenna[1] = sortedBslns[block][bsln].antenna[1];
	      temp.original = sortedBslns[block][bsln].original;
	      sortedBslns[block][bsln].antenna[0] = sortedBslns[block][bsln+1].antenna[0];
	      sortedBslns[block][bsln].antenna[1] = sortedBslns[block][bsln+1].antenna[1];
	      sortedBslns[block][bsln].original = sortedBslns[block][bsln+1].original;
	      sortedBslns[block][bsln+1].antenna[0] = temp.antenna[0];
	      sortedBslns[block][bsln+1].antenna[1] = temp.antenna[1];
	      sortedBslns[block][bsln+1].original = temp.original;
	      tempBlockPtr = crateList[block][bsln];
	      crateList[block][bsln] = crateList[block][bsln+1];
	      crateList[block][bsln+1] = tempBlockPtr;
	    }
	  }
	}
      }
    }
    if (!zoomed)
      if (doubleBandwidth)
	sprintf(filters, "1 Receiver, 4 GHz Mode: Baselines %s, blocks %s, chunks %s, sidebands %s",
		bslnFilter, blockFilter, chunkFilter, sBFilter);
      else
	sprintf(filters, "%s: Baselines %s, blocks %s, chunks %s, sidebands %s", rxFilter,
		bslnFilter, blockFilter, chunkFilter, sBFilter);
    else {
      int lBlock, lChunk, lBand;
      
      sscanf(blockFilter, "%d", &lBlock);
      sscanf(chunkFilter, "%d", &lChunk);
      lBand = 4*(lBlock-1);
      if (((lBlock > 3) && (!doubleBandwidth || (activeRx == LOW_RX_CODE))) ||
	  ((lBlock > 9) && doubleBandwidth && (activeRx == HIGH_RX_CODE)))
	lBand += 5 - lChunk;
      else
	lBand += lChunk;
      
      sprintf(filters, "%s: Baseline %s, block %s, chunk %s (band s%02d), sideband %s", rxFilter,
	      bslnFilter, blockFilter, chunkFilter, lBand, sBFilter);
    }
    if (showRefresh) {
      activeDrawable = myWindow;
      XClearWindow(myDisplay, myWindow);
    } else {
      activeDrawable = pixmap;
      XFillRectangle(myDisplay, pixmap, blackGc, 0, 0, displayWidth, displayHeight);
    }
    if (sWARMZoomed || plotSWARMOnly)
      goto processSWARM;
    /*
    printf("nB %d nBl %d nS %d nC %d\n",
	   nBlocks, nBaselines, nSidebands, nChunks);
    */
    if ((nBlocks == 0) || (nBaselines == 0) || (nSidebands == 0) ||
	(nChunks == 0)) {
      goto processSWARM;
      sayNoData(1);
      shouldSayRedrawing = TRUE;
      nChars = strlen(filters);
      XDrawImageString(myDisplay, activeDrawable, labelGc,
		       displayWidth/2 - stringWidth(filters)/2 - rightMargin/2,
		       charHeight-4, filters, nChars);
    } else {
      int foundBadCounts = FALSE;
      int badAntennaIndex[N_ANTENNAS][N_IFS][N_CRATES][N_CHUNKS];
      int blockWidth, chunkWidth, baselineHeight, i, j, k, yMaxChannel, iEf;
      int chunkLabelLine;
      float fractions[N_ANTENNAS][N_IFS][N_BLOCKS][N_CHUNKS][N_SAMPLER_LEVELS];
      float yMax, yMin;
      char channelString[80];
      
      havePlottedSomething = TRUE;
      if ((!autoscaleAmplitude) || zoomed) {
	/*
	  Loop through all chunks, and find the global yMax and yMin
	*/
	yMin = 1.0e30;
	yMax = -1.0e30;
	for (iEf = 0; iEf < N_IFS; iEf++)
	  if (doubleBandwidth || (iEf == activeRx))
	    for (bsln = 0; bsln < nBaselines; bsln++)
	      for (block = 0; block < nBlocks; block++)
		for (chunk = 0; chunk < nChunks; chunk++) {
		  chunkOffset = i = 0;
		  while (i < chunkList[chunk])
		    chunkOffset +=
		      correlator.crate[crateList[block][bsln]].description.pointsPerChunk[iEf][i++];
		  for (sb = 0; sb < nSidebands; sb++) {
		    nChannels =
		      correlator.crate[crateList[block][bsln]].description.pointsPerChunk[iEf][chunkList[chunk]];
		    if (nChannels > 0)
		      for (channel = 0; channel < nChannels; channel++) {
			amplitude = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].amp[iEf][sBList[sb]][chunkOffset+channel];
			if (logPlot) {
			  if (amplitude <= 0.0)
			    amplitude = -1000.0;
			  else
			    amplitude = log(amplitude);
			}
			if (amplitude >	yMax) {
			  yMax = amplitude;
			  yMaxChannel = channel;
			}
			if (amplitude < yMin)
			  yMin = amplitude;
		      }
		  }
		}
      }
      if ((plotFromZero) && (!logPlot))
	yMin = 0.0;
      if (checkStatistics) {
	for (iEf = 0; iEf < N_IFS; iEf++)
	  for (i = 0; i < N_ANTENNAS; i++)
	    for (j = 0; j < N_CRATES; j++)	    
	      for (k = 0; k < N_CHUNKS; k++)
		badAntennaIndex[i][iEf][j][k] = FALSE;
	for (iEf = 0; iEf < N_IFS; iEf ++)
	  if (doubleBandwidth || (activeRx == iEf))
	    for (bsln = 0; bsln < nBaselines; bsln++)
	      for (block = 0; block < nBlocks; block++)
		for (chunk = 0; chunk < N_CHUNKS; chunk++) {
		  int antIndx;
		  
		  for (antIndx = 0; antIndx < N_ANTENNAS_PER_BASELINE; antIndx++) {
		    int ant;
		    
		    ant = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[antIndx];
		    if (debugMessagesOn && 0)
		      printf("Pre-test crate = %d bsln = %d original = %d block = %d chunk = %d antIndex = %d, ant = %d\n",
			     crateList[block][bsln], bsln, sortedBslns[block][bsln].original,
			     block, chunk, antIndx, ant);
		    if (correlator.crate[crateList[block][bsln]].description.pointsPerChunk[iEf][chunk] > 0 ) {
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][0] =
			correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].counts[iEf][antIndx][chunk][0] /
			100.0;
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][1] =
			correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].counts[iEf][antIndx][chunk][1] /
			100.0;
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][2] =
			correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].counts[iEf][antIndx][chunk][2] /
			100.0;
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][3] =
			correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].counts[iEf][antIndx][chunk][3] /
			100.0;
		    } else {
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][0] = 0.16;
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][1] = 0.32;
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][2] = 0.32;
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][3] = 0.16;
		    }
		    if (debugMessagesOn)
		      printf("For ant %d, %f %f %f %f\n",
			     ant,
			     fractions[ant-1][iEf][crateList[block][bsln]][chunk][1],
			     fractions[ant-1][iEf][crateList[block][bsln]][chunk][0],
			     fractions[ant-1][iEf][crateList[block][bsln]][chunk][3],
			     fractions[ant-1][iEf][crateList[block][bsln]][chunk][2]);
		    if ((fractions[ant-1][iEf][crateList[block][bsln]][chunk][0] < badNLevelLow) ||
			(fractions[ant-1][iEf][crateList[block][bsln]][chunk][0] > badNLevelHigh) ||
			(fractions[ant-1][iEf][crateList[block][bsln]][chunk][3] < badNLevelLow) ||
			(fractions[ant-1][iEf][crateList[block][bsln]][chunk][3] > badNLevelHigh) ||
			(fractions[ant-1][iEf][crateList[block][bsln]][chunk][1] < bad1LevelLow) ||
			(fractions[ant-1][iEf][crateList[block][bsln]][chunk][1] > bad1LevelHigh) ||
			(fractions[ant-1][iEf][crateList[block][bsln]][chunk][2] < bad1LevelLow) ||
			(fractions[ant-1][iEf][crateList[block][bsln]][chunk][2] > bad1LevelHigh)) {
		      if (debugMessagesOn) {
			printf("I'm calling this one bad\n");
			printf("Ant-1 = %d, crateList[%d][%d] = %d, %f %f %f %f\n",
			       ant-1, block, bsln, crateList[block][bsln],
			       fractions[ant-1][iEf][crateList[block][bsln]][chunk][0],
			       fractions[ant-1][iEf][crateList[block][bsln]][chunk][1],
			       fractions[ant-1][iEf][crateList[block][bsln]][chunk][2],
			       fractions[ant-1][iEf][crateList[block][bsln]][chunk][3]);
		      }
		      fractions[ant-1][iEf][crateList[block][0]][chunk][0] =
			fractions[ant-1][iEf][crateList[block][bsln]][chunk][0];
		      fractions[ant-1][iEf][crateList[block][0]][chunk][1] =
			fractions[ant-1][iEf][crateList[block][bsln]][chunk][1];
		      fractions[ant-1][iEf][crateList[block][0]][chunk][2] =
			fractions[ant-1][iEf][crateList[block][bsln]][chunk][2];
		      fractions[ant-1][iEf][crateList[block][0]][chunk][3] =
			fractions[ant-1][iEf][crateList[block][bsln]][chunk][3];
		      foundBadCounts = TRUE;
		      badAntennaIndex[ant-1][iEf][crateList[block][0]][chunk] = TRUE;
		    } else if (FALSE) {
		      fractions[ant-1][iEf][crateList[block][bsln]][chunk][0] =
			fractions[ant-1][iEf][crateList[block][bsln]][chunk][1] =
			fractions[ant-1][iEf][crateList[block][bsln]][chunk][2] =
			fractions[ant-1][iEf][crateList[block][bsln]][chunk][3] = 0.0;
		      badAntennaIndex[ant-1][iEf][crateList[block][0]][chunk] = TRUE;
		      foundBadCounts = TRUE;
		    }
		  }
		}
      } else {
	char *warning = "Sampler Statistics Ignored";
	
	nChars = strlen(warning);
	XDrawImageString(myDisplay, activeDrawable, yellowGc,
			 displayWidth - stringWidth(warning)/2 - ERROR_RIGHT_MARGIN/2,
			 charHeight-4,
			 warning, nChars);
      }
      if (integrate) {
	char warning[80];
	
	if (nIntegrations < 2)
	  sprintf(warning, "Integrating (%d scan) on %s", nIntegrations, integrateSource);
	else
	  sprintf(warning, "Integrating (%d scans) on %s", nIntegrations, integrateSource);
	if ((displayWidth-60) > 3*stringWidth(warning)) {
	  nChars = strlen(warning);
	  XDrawImageString(myDisplay, activeDrawable, yellowGc,
			   1,
			   charHeight-4,
			   warning, nChars);
	}
      }
      if (foundBadCounts) {
	if (debugMessagesOn) {
	  printf("Bad counts found:\n");
	  for (iEf = 0; iEf < N_IFS; iEf++)
	    for (i = 0; i < N_ANTENNAS; i++)
	      for (j = 0; j < nBlocks; j++)
		for (k = 0; k < N_CHUNKS; k++)
		  if (badAntennaIndex[i][iEf][crateList[j][0]][k]) {
		    if (doubleBandwidth)
		      printf("Ant %d IF %d Block %d Chunk %d:\t %f %f %f %f\n",
			     i+1, iEf, crateList[j][0]+1, k+1,
			     fractions[i][iEf][crateList[j][0]][k][0],
			     fractions[i][iEf][crateList[j][0]][k][1],
			     fractions[i][iEf][crateList[j][0]][k][2],
			     fractions[i][iEf][crateList[j][0]][k][3]);
		    else
		      printf("Ant %d Block %d Chunk %d:\t %f %f %f %f\n",
			     i+1, crateList[j][0]+1, k+1,
			     fractions[i][iEf][crateList[j][0]][k][0],
			     fractions[i][iEf][crateList[j][0]][k][1],
			     fractions[i][iEf][crateList[j][0]][k][2],
			     fractions[i][iEf][crateList[j][0]][k][3]);
		  }
	}
	rightMargin = ERROR_RIGHT_MARGIN;
      } else
	if (zoomed)
	  rightMargin = ZOOMED_RIGHT_MARGIN;
	else
	  rightMargin = DEFAULT_RIGHT_MARGIN;
      nChars = strlen(filters);
      XDrawImageString(myDisplay, activeDrawable, labelGc,
		       displayWidth/2 - stringWidth(filters)/2 - rightMargin/2,
		       charHeight-4,
		       filters, nChars);
      if (zoomed) {
	blockWidth = (displayWidth - leftMargin - rightMargin -
		      (nBlocks-1)*blockSkip) / nTotalBlocks;
	chunkWidth = blockWidth;
      } else {
	blockWidth = ((displayWidth - validSWARMDataAvailable*N_SWARM_CHUNKS*displayWidth/SWARM_FRACTION)
		      - leftMargin - rightMargin - (nBlocks-1)*blockSkip) / nTotalBlocks;
	chunkWidth = blockWidth / nChunks;
      }
      if (stringWidth("128 Channels") < (chunkWidth-16)) {
	smallBottom = FALSE;
	if (zoomed)
	  bottomMargin = ZOOMED_BOTTOM_MARGIN;
	else
	  bottomMargin = DEFAULT_BOTTOM_MARGIN;
      } else {
	smallBottom = TRUE;
	bottomMargin = SMALL_BOTTOM_MARGIN;
      }
      if (zoomed || sWARMZoomed) {
	char *tip = "Left-click mouse in plot to unzoom";
	
	nChars = strlen(tip);
	XDrawImageString(myDisplay, activeDrawable, greenGc,
			 1, charHeight*2-4, tip, nChars);
      } else {
	char *tip = "Click in cell to zoom or label to filter";
	
	if ((blockWidth-50) > 3*stringWidth(tip)) {
	  if ((bslnFilter[0] != '*') ||
	      (blockFilter[0] != '*') ||
	      (chunkFilter[0] != '*') ||
	      (sBFilter[0] != '*')) {
	    int tip2Line;
	    char *tip2 = "Reset filters";
	    
	    if (bandLabeling)
	      tip2Line = 1;
	    else
	      tip2Line = 2;
	    nChars = strlen(tip2);
	    XDrawImageString(myDisplay, activeDrawable, greenGc,
			     1,
			     tip2Line*charHeight+6,
			     tip2, nChars);
	    lock_label("2");
	    if (firstLabel) {
	      lock_malloc(NULL);
	      labelBase = (label *)malloc(sizeof(label));
	      unlock_malloc(NULL);
	      labelBase->next = NULL;
	      firstLabel = FALSE;
	      labelPtr = labelBase;
	    } else {
	      label *nextLabel;
	      
	      nextLabel = labelBase;
	      while (nextLabel != NULL) {
		labelPtr = nextLabel;
		nextLabel = (label *)labelPtr->next;
	      }
	      lock_malloc(NULL);
	      labelPtr->next = (label *)malloc(sizeof(label));
	      unlock_malloc(NULL);
	      labelPtr = labelPtr->next;
	      labelPtr->next = NULL;
	    }
	    labelPtr->tlcx = 0;
	    labelPtr->tlcy = charHeight*(tip2Line-1)+6;
	    labelPtr->trcx = labelPtr->tlcx + tip2Line*stringWidth(tip2);
	    labelPtr->trcy = labelPtr->tlcy;
	    labelPtr->blcx = labelPtr->tlcx;
	    labelPtr->blcy = labelPtr->tlcy + charHeight;
	    labelPtr->brcx = labelPtr->trcx;
	    labelPtr->brcy = labelPtr->blcy;
	    labelPtr->ant1 = 1000;
	    labelPtr->iEf  = -1;
	    labelPtr->block = -1;
	    labelPtr->chunk = -1;
	    labelPtr->sb = -1;
	    labelPtr->source = -1;
	    unlock_label("2");
	  }
	  if (!bandLabeling) {
	    nChars = strlen(tip);
	    XDrawImageString(myDisplay, activeDrawable, greenGc,
			     1,
			     charHeight*2-4,
			     tip, nChars);
	  }
	}
      }
      baselineHeight = (displayHeight - topMargin - bottomMargin
			- (nBaselines-1)*baselineSkip) / nBaselines;
      if (foundBadCounts) {
	int lineNumber = 3;
	int firstLineLength = 0;
	char badCountsString[80];
	
	sprintf(badCountsString, "Bad Sampler Statistics Seen");
	nChars = strlen(badCountsString);
	XDrawImageString(myDisplay, activeDrawable, redGc,
			 displayWidth - rightMargin/2 - stringWidth(badCountsString)/2 - 18,
			 charHeight-4,
			 badCountsString, nChars);
	if (doubleBandwidth)
	  sprintf(badCountsString, "Ant IF Bl Ch    -N    -1    +1    +N");
	else
	  sprintf(badCountsString, "Ant Bl Ch    -N    -1    +1    +N");
	nChars = strlen(badCountsString);
	XDrawImageString(myDisplay, activeDrawable, redGc,
			 displayWidth - rightMargin/2 - stringWidth(badCountsString)/2 - 33,
			 2*charHeight-4,
			 badCountsString, nChars);
	for (iEf = 0; iEf < N_IFS; iEf++)
	  for (i = 0; i < N_ANTENNAS; i++)
	    for (j = 0; j < nBlocks; j++)
	      for (k = 0; k < N_CHUNKS; k++)
		if (badAntennaIndex[i][iEf][crateList[j][0]][k]) {
		  int displayBlock;
		  
		  if (doubleBandwidth && (iEf == HIGH_RX_CODE))
		    displayBlock = 12 - crateList[j][0];
		  else
		    displayBlock = crateList[j][0]+1;
		  if (doubleBandwidth)
		    sprintf(badCountsString, "%d   %d  %2d   %d     %4.1f  %4.1f  %4.1f  %4.1f",
			    i+1, iEf, displayBlock, k+1,
			    fractions[i][iEf][crateList[j][0]][k][0]*100.0,
			    fractions[i][iEf][crateList[j][0]][k][1]*100.0,
			    fractions[i][iEf][crateList[j][0]][k][2]*100.0,
			    fractions[i][iEf][crateList[j][0]][k][3]*100.0);
		  else
		    sprintf(badCountsString, "%d   %2d   %d     %4.1f  %4.1f  %4.1f  %4.1f",
			    i+1, displayBlock, k+1,
			    fractions[i][iEf][crateList[j][0]][k][0]*100.0,
			    fractions[i][iEf][crateList[j][0]][k][1]*100.0,
			    fractions[i][iEf][crateList[j][0]][k][2]*100.0,
			    fractions[i][iEf][crateList[j][0]][k][3]*100.0);
		  nChars = strlen(badCountsString);
		  if (debugMessagesOn && 0)
		    printf("stringWidth for line %d of error text = %d\n",
			   lineNumber, stringWidth(badCountsString)/2);
		  if (firstLineLength == 0)
		    firstLineLength = stringWidth(badCountsString);
		  XDrawImageString(myDisplay, activeDrawable, redGc,
				   displayWidth - rightMargin/2 - firstLineLength/2 - 33,
				   (lineNumber++)*charHeight-4,
				   badCountsString, nChars);
		}
      }
      for (bsln = 0; bsln < nBaselines; bsln++)
	for (block = 0; block < nBlocks; block++) 
	  for (iEf = 0; iEf < N_IFS; iEf++) {
	    if (((doubleBandwidth  && !plotOneBlockOnly) || (iEf == activeRx)) && !(zoomed && (iEf != activeRx))) {
	      int noLabelYet = TRUE;
	      
	      if (noLabelYet) {
		int hh, mm;
		float uTime, ss;
		char blockName[20], timeString[120];
		
		uTime = correlator.header.UTCTime[crateList[block][0]];
		hh = (int)(uTime/3600.0);
		mm = (int)((uTime-(float)(hh*3600))/60.0);
		ss = uTime - (float)(hh*3600) - (float)(mm*60);
		if (!zoomed) {
		  int eBlock;
		  
		  if (!bandLabeling) {
		    if (!doubleBandwidth || (iEf == LOW_RX_CODE)) {
		      sprintf(blockName, "Block %d", ((crateList[block][0] % 6)+1));
		    } else {
		      sprintf(blockName, "Block %d", (crateList[block][0] % 6)+7);
		    }
		    if (stringWidth(blockName) > (blockWidth - 10)) {
		      sprintf(blockName, "%d", (crateList[block][0] % 6)+1);
		    }
		    lock_label("3");
		    if (firstLabel) {
		      lock_malloc(NULL);
		      labelBase = (label *)malloc(sizeof(label));
		      unlock_malloc(NULL);
		      labelBase->next = NULL;
		      firstLabel = FALSE;
		      labelPtr = labelBase;
		    } else {
		      label *nextLabel;
		      
		      nextLabel = labelBase;
		      while (nextLabel != NULL) {
			labelPtr = nextLabel;
			nextLabel = (label *)labelPtr->next;
		      }
		      lock_malloc(NULL);
		      labelPtr->next = (label *)malloc(sizeof(label));
		      unlock_malloc(NULL);
		      labelPtr = labelPtr->next;
		      labelPtr->next = NULL;
		    }
		    sprintf(timeString, "%5.1f sec scan #%d at %02d:%02d:%05.2f",
			    correlator.header.intTime[crateList[block][0]],
			    correlator.header.scanNumber[crateList[block][0]],
			    hh, mm, ss);
		    nChars = strlen(blockName);
		    if (doubleBandwidth && (iEf != LOW_RX_CODE))
		      eBlock = block+6;
		    else
		      eBlock = block;
		    if (stringWidth(timeString) < (blockWidth - 150)) {
		      labelPtr->tlcx = leftMargin+eBlock*(blockSkip+blockWidth) +
			blockWidth/2 - stringWidth(blockName) - 30;
		      XDrawImageString(myDisplay, activeDrawable, labelGc,
				       leftMargin+eBlock*(blockSkip+blockWidth) +
				       blockWidth/2 - stringWidth(blockName) - 30,
				       charHeight*2-4,
				       blockName, nChars);
		      nChars = strlen(timeString);
		      XDrawImageString(myDisplay, activeDrawable, blueGc,
				       leftMargin+eBlock*(blockSkip+blockWidth) +
				       blockWidth/2 - 20,
				       charHeight*2-4,
				       timeString, nChars);
		    } else {
		      labelPtr->tlcx = leftMargin+eBlock*(blockSkip+blockWidth) +
			blockWidth/2 - stringWidth(blockName)/2;
		      XDrawImageString(myDisplay, activeDrawable, labelGc,
				       leftMargin+eBlock*(blockSkip+blockWidth) +
				       blockWidth/2 - stringWidth(blockName)/2,
				       charHeight*2-4,
				       blockName, nChars);
		    }
		    labelPtr->ant1 = -1;
		    labelPtr->block = crateList[block][0] % N_BLOCKS;
		    labelPtr->chunk = -1;
		    labelPtr->sb = -1;
		    labelPtr->source = -1;
		    labelPtr->iEf = -1;
		    labelPtr->tlcy = charHeight-4;
		    labelPtr->trcy = labelPtr->tlcy;
		    labelPtr->trcx = labelPtr->tlcx + 2*stringWidth(blockName);
		    labelPtr->blcx = labelPtr->tlcx;
		    labelPtr->blcy = labelPtr->tlcy + charHeight;
		    labelPtr->brcx = labelPtr->trcx;
		    labelPtr->brcy = labelPtr->blcy;
		    unlock_label("3");
		  } else { /* Band labelling */
		    lock_label("4");
		    if (firstLabel) {
		      lock_malloc(NULL);
		      labelBase = (label *)malloc(sizeof(label));
		      unlock_malloc(NULL);
		      labelBase->next = NULL;
		      firstLabel = FALSE;
		      labelPtr = labelBase;
		    } else {
		      label *nextLabel;
		      
		      nextLabel = labelBase;
		      while (nextLabel != NULL) {
			labelPtr = nextLabel;
			nextLabel = (label *)labelPtr->next;
		      }
		      lock_malloc(NULL);
		      labelPtr->next = (label *)malloc(sizeof(label));
		      unlock_malloc(NULL);
		      labelPtr = labelPtr->next;
		      labelPtr->next =  NULL;
		      labelPtr->tlcy = -1000;
		      labelPtr->trcy = -1000;
		      labelPtr->trcx = -1000;
		      labelPtr->blcx = -1000;
		      labelPtr->blcy = -1000;
		      labelPtr->brcx = -1000;
		      labelPtr->brcy = -1000;
		    }
		    unlock_label("4");
		  }
		} else { /* zoomed */
		  sprintf(timeString, "%5.1f sec scan #%d at %02d:%02d:%05.2f, max at channel %d",
			  correlator.header.intTime[crateList[block][0]],
			  correlator.header.scanNumber[crateList[block][0]],
			  hh, mm, ss,
			  yMaxChannel);
		  if (stringWidth(timeString) < (blockWidth - 150)) {
		    nChars = strlen(timeString);
		    XDrawImageString(myDisplay, activeDrawable, labelGc,
				     leftMargin +
				     blockWidth/2 - stringWidth(timeString)/2,
				     charHeight*2-4,
				     timeString, nChars);
		  }
		}
		if (!zoomed)
		  for (chunk = 0; chunk < nChunks; chunk++) {
		    char chunkName[20], chunkCount, blockCount;
		    label *nextLabel;
		    
		    if ((crateList[block][0] % 6) > 2)
		      chunkCount = nChunks - chunk - 1;
		    else
		      chunkCount = chunk;
		    if (doubleBandwidth && (iEf == HIGH_RX_CODE)) {
		      chunkCount = nChunks - chunkCount - 1;
		      blockCount = 5 - block;
		    } else
		      blockCount = block;
		    if (doubleBandwidth && (iEf == 0))
		      blockCount += nBlocks;
		    if (!bandLabeling) {
		      sprintf(chunkName, "Chunk %d", chunkList[chunk]+1);
		      chunkLabelLine = 2;
		      if (stringWidth(chunkName) > (chunkWidth-10))
			sprintf(chunkName, "%d", chunkList[chunk]+1);
		    } else {
		      int bandNumber;
		      
		      bandNumber = crateList[block][bsln]*4;
		      bandNumber %= 24;
		      if ((crateList[block][0] % 6) > 2)
			bandNumber += 4 - chunkList[chunk];
		      else
			bandNumber += chunkList[chunk]+1;
		      if (doubleBandwidth && (iEf == HIGH_RX_CODE))
			bandNumber = 49 - bandNumber;
		      sprintf(chunkName, "s%02d", bandNumber);
		      chunkLabelLine = 1;
		      if (stringWidth(chunkName) > (chunkWidth-10))
			sprintf(chunkName, "%d", bandNumber);
		    }
		    nChars = strlen(chunkName);
		    if (plotOneBlockOnly)
		      XDrawImageString(myDisplay, activeDrawable, labelGc,
				       leftMargin +
				       chunkCount*chunkWidth +
				       (blockWidth/(2*nChunks)) -
				       stringWidth(chunkName)/2,
				       charHeight*chunkLabelLine+8, chunkName, nChars);
		    else
		      XDrawImageString(myDisplay, activeDrawable, labelGc,
				       leftMargin+blockCount*(blockSkip+blockWidth) +
				       chunkCount*chunkWidth +
				       (blockWidth/(2*nChunks)) -
				       stringWidth(chunkName)/2,
				       charHeight*chunkLabelLine+8, chunkName, nChars);
		    lock_label("5");
		    nextLabel = labelBase;
		    while (nextLabel != NULL) {
		      labelPtr = nextLabel;
		      nextLabel = (label *)labelPtr->next;
		    }
		    lock_malloc(NULL);
		    labelPtr->next = (label *)malloc(sizeof(label));
		    unlock_malloc(NULL);
		    labelPtr = labelPtr->next;
		    labelPtr->next = NULL;
		    labelPtr->tlcx = leftMargin+blockCount*(blockSkip+blockWidth) +
		      chunkCount*chunkWidth + (blockWidth/(2*nChunks)) -
		      stringWidth(chunkName)/2 - 3;
		    labelPtr->tlcy = charHeight*(chunkLabelLine-1)+8;
		    labelPtr->trcx = labelPtr->tlcx + stringWidth(chunkName) + 8;
		    labelPtr->trcy = labelPtr->tlcy;
		    labelPtr->blcx = labelPtr->tlcx;
		    labelPtr->blcy = labelPtr->tlcy + charHeight;
		    labelPtr->brcx = labelPtr->trcx;
		    labelPtr->brcy = labelPtr->blcy;
		    labelPtr->ant1 = -1;
		    if (bandLabeling)
		      labelPtr->block = crateList[block][0] % N_BLOCKS;
		    else
		      labelPtr->block = -1;
		    labelPtr->chunk = chunkList[chunk];
		    labelPtr->iEf = iEf;
		    labelPtr->sb = -1;
		    labelPtr->source = -1;
		    unlock_label("5");
		  }
		noLabelYet = FALSE;
	      }
	      if (block == 0) {
		char bslnString[10];
		
		if (!zoomed) {
		  label *nextLabel;
		  
		  sprintf(bslnString, "%d-%d",
			  sortedBslns[block][bsln].antenna[0],
			  sortedBslns[block][bsln].antenna[1]);
		  nChars = strlen(bslnString);
		  XDrawImageString(myDisplay, activeDrawable, labelGc,
				   0,
				   topMargin+bsln*(baselineSkip+baselineHeight) +
				   baselineHeight/2 + charHeight/2,
				   bslnString, nChars);
		  lock_label("6");
		  nextLabel = labelBase;
		  while (nextLabel != NULL) {
		    labelPtr = nextLabel;
		    nextLabel = (label *)labelPtr->next;
		  }
		  lock_malloc(NULL);
		  labelPtr->next = (label *)malloc(sizeof(label));
		  unlock_malloc(NULL);
		  labelPtr = labelPtr->next;
		  labelPtr->next = NULL;
		  labelPtr->tlcx = 0;
		  labelPtr->tlcy = topMargin+bsln*(baselineSkip+baselineHeight) +
		    baselineHeight/2 - charHeight/2;
		  labelPtr->trcx = labelPtr->tlcx + 2* stringWidth(bslnString);
		  labelPtr->trcy = labelPtr->tlcy;
		  labelPtr->blcx = labelPtr->tlcx;
		  labelPtr->blcy = labelPtr->tlcy + charHeight;
		  labelPtr->brcx = labelPtr->trcx;
		  labelPtr->brcy = labelPtr->blcy;
		  labelPtr->ant1 = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[0];
		  labelPtr->ant2 = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[1];
		  labelPtr->block = -1;
		  labelPtr->chunk = -1;
		  labelPtr->iEf = DONT_CHANGE_RX;
		  labelPtr->sb = -1;
		  labelPtr->source = -1;
		  unlock_label("6");
		}
		if (nSidebands > 1) {
		  label *nextLabel;
		  
		  if (baselineHeight > (3*charHeight)) {
		    sprintf(bslnString, "USB");
		    nChars = strlen(bslnString);
		    XDrawImageString(myDisplay, activeDrawable, blueGc,
				     0,
				     topMargin+bsln*(baselineSkip+baselineHeight) +
				     baselineHeight/4 + charHeight/2,
				     bslnString, nChars);
		    lock_label("7");
		    if (firstLabel) {
		      lock_malloc(NULL);
		      labelBase = (label *)malloc(sizeof(label));
		      unlock_malloc(NULL);
		      labelBase->next = (label *)NULL;
		      firstLabel = FALSE;
		      labelPtr = labelBase;
		    } else {
		      nextLabel = labelBase;
		      while (nextLabel != NULL) {
			labelPtr = nextLabel;
			nextLabel = (label *)labelPtr->next;
		      }
		      lock_malloc(NULL);
		      labelPtr->next = (label *)malloc(sizeof(label));
		      unlock_malloc(NULL);
		      labelPtr = labelPtr->next;
		      labelPtr->next = NULL;
		    }
		    labelPtr->tlcx = 0;
		    labelPtr->tlcy = topMargin+bsln*(baselineSkip+baselineHeight) +
		      baselineHeight/4 - charHeight/2;
		    labelPtr->trcx = labelPtr->tlcx + stringWidth(bslnString);
		    labelPtr->trcy = labelPtr->tlcy;
		    labelPtr->blcx = labelPtr->tlcx;
		    labelPtr->blcy = labelPtr->tlcy + charHeight;
		    labelPtr->brcx = labelPtr->trcx;
		    labelPtr->brcy = labelPtr->blcy;
		    labelPtr->ant1 = -1;
		    labelPtr->ant2 = -1;
		    labelPtr->block = -1;
		    labelPtr->chunk = -1;
		    labelPtr->iEf = -1;
		    labelPtr->sb = 1;
		    labelPtr->source = -1;
		    unlock_label("7");
		    sprintf(bslnString, "LSB");
		    nChars = strlen(bslnString);
		    XDrawImageString(myDisplay, activeDrawable, blueGc,
				     0,
				     topMargin+bsln*(baselineSkip+baselineHeight) +
				     3*baselineHeight/4 + charHeight/2,
				     bslnString, nChars);
		    lock_label("8");
		    nextLabel = labelBase;
		    while (nextLabel != NULL) {
		      labelPtr = nextLabel;
		      nextLabel = (label *)labelPtr->next;
		    }
		    lock_malloc(NULL);
		    labelPtr->next = (label *)malloc(sizeof(label));
		    unlock_malloc(NULL);
		    labelPtr = labelPtr->next;
		    labelPtr->next = NULL;
		    labelPtr->tlcx = 0;
		    labelPtr->tlcy = topMargin+bsln*(baselineSkip+baselineHeight) +
		      3*baselineHeight/4 - charHeight/2;
		    labelPtr->trcx = labelPtr->tlcx + stringWidth(bslnString);
		    labelPtr->trcy = labelPtr->tlcy;
		    labelPtr->blcx = labelPtr->tlcx;
		    labelPtr->blcy = labelPtr->tlcy + charHeight;
		    labelPtr->brcx = labelPtr->trcx;
		    labelPtr->brcy = labelPtr->blcy;
		    labelPtr->ant1 = -1;
		    labelPtr->ant2 = -1;
		    labelPtr->iEf = -1;
		    labelPtr->block = -1;
		    labelPtr->chunk = -1;
		    labelPtr->sb = 0;
		    labelPtr->source = -1;
		    unlock_label("8");
		  }
		}
	      }
	      for (chunk = 0; chunk < nChunks; chunk++) {
		int channelWidth, chunkCount, blockCount;
		XPoint box[5];
		XPoint data[N_CHANNELS_MAX+1];
		float yOffset, yScale;
		
		if ((crateList[block][0] % 6) > 2)
		  chunkCount = nChunks - chunk - 1;
		else
		  chunkCount = chunk;
		if (doubleBandwidth && (iEf == HIGH_RX_CODE) && !zoomed) {
		  chunkCount = nChunks - chunkCount - 1;
		  blockCount = nBlocks - block - 1;
		} else
		  blockCount = block;
		if (doubleBandwidth && (iEf == 0) && !zoomed)
		  blockCount += nBlocks;
		if (plotOneBlockOnly)
		  blockCount = 0;
		/*
		  Draw the little box for the spectrum
		*/
		box[0].x = box[4].x = chunkCount*chunkWidth +
		  leftMargin+blockCount*(blockSkip+blockWidth) - 2;
		box[0].y = box[4].y =
		  topMargin+bsln*(baselineSkip+baselineHeight)+1;
		box[1].x = box[0].x + chunkWidth;
		if (box[1].x > legacyEnd)
		  legacyEnd = box[1].x;
		box[1].y = box[0].y;
		box[2].x = box[1].x;
		box[2].y = box[1].y+baselineHeight;
		box[3].y = box[2].y;
		box[3].x = box[0].x;
		if ((badAntennaIndex[correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[0]-1][iEf][crateList[block][0]][chunkList[chunk]] ||
		     badAntennaIndex[correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[1]-1][iEf][crateList[block][0]][chunkList[chunk]]) &&
		    checkStatistics)
		  useRed = TRUE;
		else
		  useRed = FALSE;
		if (useRed)
		  XDrawLines(myDisplay, activeDrawable, redGc, box, 5,
			     CoordModeOrigin);
		else
		  XDrawLines(myDisplay, activeDrawable, blueGc, box, 5,
			     CoordModeOrigin);
		if (nSidebands > 1) {
		  /*
		    Draw line separating the two sidebands
		  */
		  if ((badAntennaIndex[correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[0]-1][iEf][crateList[block][0]][chunk] ||
		       badAntennaIndex[correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[1]-1][iEf][crateList[block][0]][chunk]) &&
		      checkStatistics)
		    XDrawLine(myDisplay, activeDrawable, redGc,
			      chunkCount*chunkWidth +
			      leftMargin+blockCount*(blockSkip+blockWidth) - 2,
			      topMargin+bsln*(baselineSkip+baselineHeight)+1+
			      baselineHeight/2,
			      leftMargin + blockCount*(blockSkip + blockWidth) +
			      (nChunks - chunk)*chunkWidth - 2,
			      topMargin+bsln*(baselineSkip+baselineHeight)+1+
			      baselineHeight/2);
		  else
		    XDrawLine(myDisplay, activeDrawable, blueGc,
			      chunkCount*chunkWidth +
			      leftMargin+blockCount*(blockSkip+blockWidth) - 2,
			      topMargin+bsln*(baselineSkip+baselineHeight)+1+
			      baselineHeight/2,
			      leftMargin + blockCount*(blockSkip + blockWidth) +
			      (nChunks - chunk)*chunkWidth - 2,
			      topMargin+bsln*(baselineSkip+baselineHeight)+1+
			      baselineHeight/2);
		}
		for (sb = 0; sb < nSidebands; sb++) {
		  {
		    /* Check to see if we should mark any spectral lines */
		    int band, deltax, deltay, theBlock;
		    markListEntry *ptr;
		    XPoint tranLine[3];
		    
		    theBlock = crateList[block][0] % N_BLOCKS;
		    band = 4*theBlock;
		    if (theBlock > 2)
		      band += 4 - chunkList[chunk];
		    else
		      band += chunkList[chunk] + 1;
		    dprintf("block: %d chunk: %d (%d,%d) band: %d\n", theBlock, chunkList[chunk], block, chunk, band);
		    ptr = markListRoot;
		    while (ptr != NULL) {
		      if ((ptr->band == band) && (sBList[sb] == ptr->sB)) {
			dprintf("Should draw %s line here\n", ptr->name);
			deltax = (int)((((float)chunkWidth)*ptr->offset/104.0e6) +0.5);
			/*
			  if (sBList[sb] < 1)
			  deltax *= -1;
			*/
			tranLine[0].x = tranLine[1].x = deltax + chunkCount*chunkWidth + chunkWidth/2 +
			  leftMargin+blockCount*(blockSkip+blockWidth) - 2;
			if (nSidebands == 1) {
			  dprintf("Doing a SSB line\n");
			  tranLine[0].y = topMargin+bsln*(baselineSkip+baselineHeight)+1;
			  tranLine[1].y = tranLine[0].y + baselineHeight;
			} else if (sb == 0) {
			  dprintf("Doing a LSB line\n");
			  tranLine[0].y = topMargin+bsln*(baselineSkip+baselineHeight)+1 + baselineHeight/2;
			  tranLine[1].y = tranLine[0].y + baselineHeight/2;
			} else {
			  dprintf("Doing a USB line\n");
			  tranLine[0].y = topMargin+bsln*(baselineSkip+baselineHeight)+1;
			  tranLine[1].y = tranLine[0].y + baselineHeight/2;
			}
			if (zoomed)
			  deltay = charHeight;
			else
			  deltay = 0;
			XDrawLines(myDisplay, activeDrawable, yellowGc, tranLine, 2,
				   CoordModeOrigin);
			nChars = strlen(ptr->name);
			XDrawImageString(myDisplay, activeDrawable, yellowGc,
					 leftMargin+blockCount*(blockSkip+blockWidth) +
					 chunkCount*chunkWidth + deltax +
					 (blockWidth/(2*nChunks)) -
					 stringWidth(ptr->name)/2,
					 deltay + charHeight*chunkLabelLine+8, ptr->name, nChars);
		      }
		      ptr = ptr->next;
		    }
		  }
		  chunkOffset = i = 0;
		  while (i < chunkList[chunk])
		    chunkOffset +=
		      correlator.crate[crateList[block][bsln] % 6].description.pointsPerChunk[iEf][i++];
		  lock_cell("2");
		  if (firstCell) {
		    lock_malloc(NULL);
		    cellRoot = (cell *)malloc(sizeof(cell));
		    unlock_malloc(NULL);
		    cellRoot->next = NULL;
		    firstCell = FALSE;
		    cellPtr = cellRoot;
		  } else {
		    cell *nextCell;
		    
		    nextCell = cellRoot;
		    while (nextCell != NULL) {
		      cellPtr = nextCell;
		      nextCell = (cell *)cellPtr->next;
		    }
		    lock_malloc(NULL);
		    cellPtr->next = (cell *)malloc(sizeof(cell));
		    unlock_malloc(NULL);
		    cellPtr = cellPtr->next;
		    cellPtr->next = NULL;
		  }
		  cellPtr->tlcx = box[0].x;
		  cellPtr->tlcy = box[0].y;
		  cellPtr->trcx = box[1].x;
		  cellPtr->trcy = box[1].y;
		  cellPtr->blcx = box[3].x;
		  cellPtr->blcy = box[3].y;
		  cellPtr->brcx = box[2].x;
		  cellPtr->brcy = box[2].y;
		  if (nSidebands > 1) {
		    if (sBList[sb] == 0) {
		      cellPtr->tlcy = box[0].y + baselineHeight/2;
		      cellPtr->trcy = box[1].y + baselineHeight/2;
		    } else {
		      cellPtr->blcy = box[3].y - baselineHeight/2;
		      cellPtr->brcy = box[2].y - baselineHeight/2;
		    }
		  }
		  cellPtr->iEf = iEf;
		  cellPtr->ant1 = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[0];
		  cellPtr->ant2 = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].antenna[1];
		  cellPtr->block = crateList[block][0] % N_BLOCKS;
		  cellPtr->chunk = chunkList[chunk];
		  cellPtr->sb = sBList[sb];
		  cellPtr->source = -1;
		  unlock_cell("2");
		  nChannels =
		    correlator.crate[crateList[block][bsln] % 6].description.pointsPerChunk[iEf][chunkList[chunk]];
		  if (nChannels > 0) {
		    if (autoscaleAmplitude) {
		      yMin = 1.0e30;
		      yMax = -1.0e30;
		      for (channel = 0; channel < nChannels; channel++) {
			amplitude = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].amp[iEf][sBList[sb]][chunkOffset+channel];
			if (logPlot) {
			  if (amplitude <= 0.0)
			    amplitude = -10000.0;
			  else
			    amplitude = log(amplitude);
			}
			if (amplitude > yMax) {
			  yMax = amplitude;
			  yMaxChannel = channel;
			}
			if (amplitude < yMin)
			  yMin = amplitude;
		      }
		    }
		    if ((plotFromZero) && (!logPlot))
		      yMin = 0.0;
		    if (nSidebands > 1)
		      yScale = ((float)((baselineHeight-6)/2))/((float)(yMax-yMin));
		    else
		      yScale = ((float)(baselineHeight-4))/((float)(yMax-yMin));
		    yOffset = (float)yMin;
		    channelWidth = chunkWidth/nChannels;
		    for (channel = 0; channel < nChannels; channel++) {
		      if ((crateList[block][0] < (N_BLOCKS/2)) || (!FLIP_CHANNELS))
			data[channel].x = -1 + chunkCount*chunkWidth + leftMargin + blockCount*blockWidth +
			  blockCount*blockSkip +
			  (int)((float)(chunkWidth-2)*(float)channel/(float)(nChannels-1));
		      else
			data[channel].x = -1 + chunkCount*chunkWidth + leftMargin + blockCount*blockWidth +
			  blockCount*blockSkip +
			  (int)((float)(chunkWidth-2)*(float)(nChannels - channel - 1)/(float)(nChannels-1));
		      
		      amplitude = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].amp[iEf][sBList[sb]][chunkOffset+channel];
		      if ((logPlot) && (showAmp)) {
			if (amplitude <= 0.0)
			  amplitude = -10000.0;
			else
			  amplitude = log(amplitude);
		      }
		      if (nSidebands > 1)
			data[channel].y = -1 + topMargin + baselineHeight/2 + (1-sb)*(baselineHeight/2 + 2) +
			  bsln*(baselineSkip+baselineHeight) -
			  (int)((amplitude - yOffset) * yScale);
		      else
			data[channel].y = -1 + topMargin + baselineHeight + bsln*(baselineSkip+baselineHeight) -
			  (int)((amplitude - yOffset) * yScale);
		    }
		    if (showLags) {
		      int ii;
		      float *fFTBuf;
		      float *lags;
		      float lagMax, lagMin;
		      
		      fFTBuf = (float *)malloc(4*nChannels*sizeof(float) + 3);
		      if (fFTBuf == NULL) {
			perror("malloc of fFTBuf");
			exit(-1);
		      }
		      lags = (float *)malloc(2*nChannels*sizeof(float) + 3);
		      if (lags == NULL) {
			perror("malloc of lags");
			exit(-1);
		      }
		      for (ii = 0; ii < nChannels; ii++) {
			float tAmp, tPhase, tReal, tImag;
			
			tAmp = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].amp[iEf][sBList[sb]][chunkOffset+ii];
			tPhase =  correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].phase[iEf][sBList[sb]][chunkOffset+ii];
			tReal = tAmp*cos(tPhase);
			tImag = tAmp*sin(tPhase);
			fFTBuf[(2*ii)+1] = tReal;
			fFTBuf[(2*ii)+2] = tImag;
			fFTBuf[2*(2*nChannels-ii-1)+1] = tReal;
			fFTBuf[2*(2*nChannels-ii-1)+2] = -tImag;
		      }
		      four1(&fFTBuf[0], 2*nChannels, -1);
		      for (ii = 0; ii < nChannels; ii++) {
			lags[2*ii] = fFTBuf[(2*ii)+1];
			lags[2*ii+1] = -fFTBuf[4*nChannels - (2*ii) - 1];
		      }
		      lagMax = -1.0e30;
		      lagMin = 1.0e30;
		      for (ii = 0; ii < 2*nChannels; ii++) {
			if (lags[ii] < lagMin)
			  lagMin = lags[ii];
			if (lags[ii] > lagMax)
			  lagMax = lags[ii];
		      }
		      if (nSidebands > 1)
			yScale = ((float)((baselineHeight-6)/2))/((float)(lagMax-lagMin));
		      else
			yScale = ((float)(baselineHeight-4))/((float)(lagMax-lagMin));
		      yOffset = (float)lagMin;
		      for (ii = 0; ii < 2*nChannels; ii++) {
			if (nSidebands > 1)
			  data[ii].y = -1 + topMargin + baselineHeight/2 + (1-sb)*(baselineHeight/2 + 2) +
			    bsln*(baselineSkip+baselineHeight) -
			    (int)((lags[ii] - yOffset) * yScale);
			else
			  data[ii].y = -1 + topMargin + baselineHeight + bsln*(baselineSkip+baselineHeight) -
			    (int)((lags[ii] - yOffset) * yScale);
			if (((crateList[block][0] % 6) < (N_BLOCKS/2)) || (!FLIP_CHANNELS))
			  data[ii].x = -1 + chunkCount*chunkWidth + leftMargin + blockCount*blockWidth +
			    blockCount*blockSkip +
			    (int)((float)(chunkWidth-2)*(float)ii/(float)(2*nChannels-1));
			else
			  data[ii].x = -1 + chunkCount*chunkWidth + leftMargin + blockCount*blockWidth +
			    blockCount*blockSkip +
			    (int)((float)(chunkWidth-2)*(float)(2*nChannels - ii - 1)/(float)(2*nChannels-1));
		      }
		      XDrawPoints(myDisplay, activeDrawable, whiteGc, data, 2*nChannels,
				  CoordModeOrigin);
		      free(lags);
		      free(fFTBuf);
		    }
		    if (showAmp) {
		      if (channelWidth < 3) {
			XDrawLines(myDisplay, activeDrawable, blueGc, data, nChannels,
				   CoordModeOrigin);
		      } else {
			int ii;
			XPoint hist[2050];
			
			for (ii = 0; ii < nChannels; ii++) {
			  if (ii == 0)
			    hist[2*ii].x = data[ii].x;
			  else
			    hist[2*ii].x = hist[2*(ii-1)+1].x;
			  if (ii == (nChannels-1))
			    hist[2*ii+1].x = data[ii].x;
			  else
			    hist[2*ii+1].x = data[ii].x+channelWidth/2;
			  hist[2*ii].y = hist[2*ii+1].y = data[ii].y;
			}
			XDrawLines(myDisplay, activeDrawable, blueGc, hist, 2*nChannels,
				   CoordModeOrigin);
		      }
		    }
		    if (nSidebands > 1)
		      yScale = ((float)((baselineHeight-5)/2))/(2.0 * M_PI);
		    else
		      yScale = ((float)(baselineHeight-3))/(2.0 * M_PI);
		    for (channel = 0; channel < nChannels; channel++) {
		      if (nSidebands > 1)
			data[channel].y = topMargin + baselineHeight/2 + (1-sb)*(baselineHeight/2 + 2) +
			  bsln*(baselineSkip+baselineHeight) - 2 -
			  (int)(((float)correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].phase[iEf][sBList[sb]][chunkOffset+channel]+M_PI) *
				yScale);
		      else
			data[channel].y = topMargin + baselineHeight + bsln*(baselineSkip+baselineHeight) - 2 -
			  (int)(((float)correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].phase[iEf][sBList[sb]][chunkOffset+channel]+M_PI) *
				yScale);
		      if (zoomed) {
			cellAmp[channel] = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].amp[iEf][sBList[sb]][chunkOffset+channel];
			cellPhase[channel] = correlator.crate[crateList[block][bsln]].data[sortedBslns[block][bsln].original].phase[iEf][sBList[sb]][chunkOffset+channel] * 180.0 / M_PI;
		      }
		    }
		    if (zoomed) {
		      cellX0 = data[0].x;
		      cellInc = (float)chunkWidth/(float)(nChannels-1);
		      cellNChannels = nChannels;
		    }
		    if (showPhase) {
		      if (((channelWidth < 3) || (baselineHeight < 100)) && (userSelectedPointSize == 0))
			XDrawPoints(myDisplay, activeDrawable, whiteGc, data, nChannels,
				    CoordModeOrigin);
		      else {
			int ii;
			
			for (ii = 0; ii < nChannels; ii++) {
			  int pointSize;
			  
			  if (channelWidth < 26)
			    pointSize = channelWidth + userSelectedPointSize;
			  else
			    pointSize = 26 + userSelectedPointSize;
			  if (userSelectedPointSize > 0)
			    pointSize--;
			  XFillArc(myDisplay, activeDrawable, whiteGc,
				   data[ii].x-pointSize/2, data[ii].y,
				   pointSize, pointSize, 0, 360*64);
			}
		      }
		    } /* if (showPhase) */
		    chunkOffset += nChannels;
		    if (!zoomed) {
		      if (!smallBottom) {
			if (bsln == (nBaselines-1)) {
			  sprintf(channelString, "%d Channels", nChannels);
			  nChars = strlen(channelString);
			  XDrawImageString(myDisplay, activeDrawable, labelGc,
					   leftMargin + chunkCount*chunkWidth + chunkWidth/2 - stringWidth(channelString)/2 +
					   blockCount*(blockWidth+blockSkip),
					   displayHeight,
					   channelString, nChars);
			}
		      }
		    } else {
		      if (!smallBottom) {
			int tc, cO, tl, channelStep, minInc, goal;
			float channelInc;
			char tickLabel[10];
			
			if (scaleInMHz)
			  goal = 104;
			else if (showLags)
			  goal = 2*nChannels;
			else
			  goal = nChannels;
			channelInc = (float)chunkWidth/(float)(goal-1);
			minInc = 35;
			if ((10*blockWidth / goal) > minInc)
			  channelStep = 10;
			else if ((20*blockWidth / goal) > minInc)
			  channelStep = 20;
			else if ((50*blockWidth / goal) > minInc)
			  channelStep = 50;
			else if ((100*blockWidth / goal) > minInc)
			  channelStep = 100;
			else channelStep = 200;
			for (tc = 0; tc < goal; tc += channelStep) {
			  if (((crateList[block][0] % 6) < (N_BLOCKS/2)) || (!FLIP_CHANNELS))
			    cO = (int)(((float)tc)*channelInc + 0.5);
			  else
			    cO = (int)(((float)(goal - tc - 1))*channelInc + 0.5);
			  if (tc % 100)
			    tl = 5;
			  else
			    tl = 11;
			  sprintf(tickLabel, "%d", tc);
			  nChars = strlen(tickLabel);
			  XDrawImageString(myDisplay, activeDrawable, labelGc,
					   chunkCount*chunkWidth - stringWidth(tickLabel) +
					   leftMargin+block*(blockSkip+blockWidth) + 2 + cO,
					   displayHeight,
					   tickLabel, nChars);
			  if (useRed) {
			    if (tc > 0)
			      XDrawLine(myDisplay, activeDrawable, redGc,
					chunkCount*chunkWidth +
					leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
					topMargin+bsln*(baselineSkip+baselineHeight) + 1,
					chunkCount*chunkWidth +
					leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
					topMargin+bsln*(baselineSkip+baselineHeight) + 1 -
					tl);
			    XDrawLine(myDisplay, activeDrawable, redGc,
				      chunkCount*chunkWidth +
				      leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
				      topMargin+bsln*(baselineSkip+baselineHeight) + 1 +
				      baselineHeight,
				      chunkCount*chunkWidth +
				      leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
				      topMargin+bsln*(baselineSkip+baselineHeight) + 1 +
				      baselineHeight + tl);
			  } else {
			    if (tc > 0)
			      XDrawLine(myDisplay, activeDrawable, blueGc,
					chunkCount*chunkWidth +
					leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
					topMargin+bsln*(baselineSkip+baselineHeight) + 1,
					chunkCount*chunkWidth +
					leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
					topMargin+bsln*(baselineSkip+baselineHeight) + 1 -
					tl);
			    XDrawLine(myDisplay, activeDrawable, blueGc,
				      chunkCount*chunkWidth +
				      leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
				      topMargin+bsln*(baselineSkip+baselineHeight) + 1 +
				      baselineHeight,
				      chunkCount*chunkWidth +
				      leftMargin+block*(blockSkip+blockWidth) - 2 + cO,
				      topMargin+bsln*(baselineSkip+baselineHeight) + 1 +
				      baselineHeight + tl);
			  }
			}
		      }
		    }
		  }
		}
	      }
	    }
	  }
    processSWARM:
      if (validSWARMDataAvailable) {
	int bsln, a1, a2, chunk, sb, corrBsln;
	int bsln2A1[45], bsln2A2[45], nBsln, bsln2Sorted[45];
	int corrBaselineMapping[8][8], nANPattern[8];
	int sWARMChunkWidth, sWARMChunkHeight, i;
	char scratchString[100];
	  
	/* Derive the NAN pattern */
	bsln = 0;
	while (!correlator.sWARMBaseline[bsln].haveCrossData)
	  bsln++;
	for (i = 0; i < 8; i++)
	  if (isnan(correlator.sWARMBaseline[bsln].amp[0][0][8*i]))
	    nANPattern[i] = FALSE;
	  else
	    nANPattern[i] = TRUE;
	for (i = 0; i < 8; i++)
	  if (nANPattern[i])
	    sWARMNANPatternString[i] = 'D';
	  else
	    sWARMNANPatternString[i] = 'N';
	sWARMNANPatternString[8] = (char)0;
	bsln = 0;
	corrBsln = 0;
	for (a1 = 1; a1 < 8; a1++)
	  for (a2 = a1+1; a2 <= 8; a2++) {
	    corrBaselineMapping[a1-1][a2-1] = corrBsln++;
	    if ((antennaInArray[a1] && antennaInArray[a2]) && bslnPlottable(a1, a2)) {
	      bsln2A1[bsln] = a1;
	      bsln2A2[bsln] = a2;
	      bsln++;
	    }
	  }
	nBsln = bsln;
	for (bsln = 0; bsln < nBsln; bsln++)
	  bsln2Sorted[bsln] = bsln;
	if ((nBsln > 1) && (bslnOrder)) {
	  int swapNeeded;
	  
	  do {
	    swapNeeded = FALSE;
	    for (bsln = 0; bsln < nBsln-1; bsln++)
	      if (bslnLength[bsln2A1[bsln2Sorted[bsln]]][bsln2A2[bsln2Sorted[bsln]]] >
		  bslnLength[bsln2A1[bsln2Sorted[bsln+1]]][bsln2A2[bsln2Sorted[bsln+1]]]) {
		int temp;
		
		temp = bsln2Sorted[bsln];
		bsln2Sorted[bsln] = bsln2Sorted[bsln+1];
		bsln2Sorted[bsln+1] = temp;
		swapNeeded = TRUE;
	      }
	  } while (swapNeeded);
	}
	if (!sWARMZoomed) {
	  int nAveraged;
	  int chunksListed = 0;
	  label *nextLabel;

	  if (plotSWARMOnly || TRUE) {
	    int i, tickStep, plotX0, plotY0;
	    float xScale;
	    XPoint tick[2];

	    /* I get sent here if none of the legacy chunks have been plotted */
	    if (nBsln > 0)
	      baselineHeight = (displayHeight - topMargin - 3*DEFAULT_BOTTOM_MARGIN
				- (nBsln-1)*baselineSkip) / nBsln;
	    else {
	      fprintf(stderr, "nBsln = %d\n", nBsln);
	      baselineHeight = 10;
	    }
	    legacyEnd = leftMargin;
	    chunkLabelLine = 1;
	    rightMargin = DEFAULT_RIGHT_MARGIN;
	    sWARMChunkWidth = displayWidth - leftMargin - rightMargin- 8;
	    if (sWARMChunkWidth > (3*stringWidth("Reset filters"))) {
	      int tip2Line;
	      char *tip2 = "Reset filters";
	      
	      if (bandLabeling)
		tip2Line = 1;
	      else
		tip2Line = 2;
	      nChars = strlen(tip2);
	      XDrawImageString(myDisplay, activeDrawable, greenGc,
			       1,
			       tip2Line*charHeight+6,
			       tip2, nChars);
	      lock_label("SWARM-2");
	      if (firstLabel) {
		lock_malloc(NULL);
		labelBase = (label *)malloc(sizeof(label));
		unlock_malloc(NULL);
		labelBase->next = NULL;
		firstLabel = FALSE;
		labelPtr = labelBase;
	      } else {
		label *nextLabel;
		
		nextLabel = labelBase;
		while (nextLabel != NULL) {
		  labelPtr = nextLabel;
		  nextLabel = (label *)labelPtr->next;
		}
		lock_malloc(NULL);
		labelPtr->next = (label *)malloc(sizeof(label));
		unlock_malloc(NULL);
		labelPtr = labelPtr->next;
		labelPtr->next = NULL;
	      }
	      labelPtr->tlcx = 0;
	      labelPtr->tlcy = charHeight*(tip2Line-1)+6;
	      labelPtr->trcx = labelPtr->tlcx + tip2Line*stringWidth(tip2);
	      labelPtr->trcy = labelPtr->tlcy;
	      labelPtr->blcx = labelPtr->tlcx;
	      labelPtr->blcy = labelPtr->tlcy + charHeight;
	      labelPtr->brcx = labelPtr->trcx;
	      labelPtr->brcy = labelPtr->blcy;
	      labelPtr->ant1 = 1000;
	      labelPtr->iEf  = -1;
	      labelPtr->block = -1;
	      labelPtr->chunk = -1;
	      labelPtr->sb = -1;
	      labelPtr->source = -1;
	      unlock_label("SWARM - 2");
	    }
	    for (bsln = 0; bsln < nBsln; bsln++) {
	      sprintf(scratchString, "%d-%d", bsln2A1[bsln2Sorted[bsln]], bsln2A2[bsln2Sorted[bsln]]);
	      nChars = strlen(scratchString);
	      XDrawImageString(myDisplay, activeDrawable, labelGc,
			       0, topMargin+bsln*(baselineSkip+baselineHeight) +
			       baselineHeight/2 + charHeight/2 - 2,
			       scratchString, nChars);
	      lock_label("SWARM-22");
	      if (firstLabel) {
		lock_malloc(NULL);
		labelBase = (label *)malloc(sizeof(label));
		unlock_malloc(NULL);
		labelBase->next = NULL;
		firstLabel = FALSE;
		labelPtr = labelBase;
	      } else {
		label *nextLabel;
		
		nextLabel = labelBase;
		while (nextLabel != NULL) {
		  labelPtr = nextLabel;
		  nextLabel = (label *)labelPtr->next;
		}
		lock_malloc(NULL);
		labelPtr->next = (label *)malloc(sizeof(label));
		unlock_malloc(NULL);
		labelPtr = labelPtr->next;
		labelPtr->next = NULL;
	      }
	      labelPtr->tlcx = 0; labelPtr->tlcy = topMargin+bsln*(baselineSkip+baselineHeight) + baselineHeight/2;
	      labelPtr->trcx = labelPtr->tlcx + stringWidth("8-8") + 2;
	      labelPtr->trcy = labelPtr->tlcy;
	      labelPtr->blcx = labelPtr->tlcx;
	      labelPtr->blcy = labelPtr->tlcy + charHeight;
	      labelPtr->brcx = labelPtr->trcx;
	      labelPtr->brcy = labelPtr->blcy;
	      labelPtr->ant1 = bsln2A1[bsln2Sorted[bsln]];
	      labelPtr->ant2 = bsln2A2[bsln2Sorted[bsln]];
	      labelPtr->iEf  = DONT_CHANGE_RX;
	      labelPtr->block = -1;
	      labelPtr->chunk = -1;
	      labelPtr->sb = -1;
	      labelPtr->source = -1;
	      unlock_label("SWARM - 2");
	      if ((baselineHeight > (2*charHeight))
		  && (sBFilter[0] == '*')) {
		XDrawImageString(myDisplay, activeDrawable, blueGc,
				 5, topMargin+bsln*(baselineSkip+baselineHeight) + baselineSkip/2 + charHeight,
				 "USB", 3);
		XDrawImageString(myDisplay, activeDrawable, blueGc,
				 5, topMargin+bsln*(baselineSkip+baselineHeight) + baselineHeight + baselineSkip/2 - 4,
				 "LSB", 3);
	      }
	    }
	    if (sWARMChunkWidth > 20*stringWidth("16000"))
	      tickStep = 1000;
	    else if (sWARMChunkWidth > 10*stringWidth("16000"))
	      tickStep = 2000;
	    else
	      tickStep = 4000;
	    plotX0 = leftMargin + 4; plotY0 = charHeight*2 - 7;
	    xScale = ((float)sWARMChunkWidth)/((float) N_SWARM_CHANNELS);
	    for (i = 0; i < N_SWARM_CHANNELS; i += tickStep) {
	      tick[0].x = plotX0+(int)(i*xScale) + 1; tick[0].y = plotY0 + nBsln*(baselineSkip+baselineHeight) + 2;
	      tick[1].x = tick[0].x;                  tick[1].y = tick[0].y+5;
	      XDrawLines(myDisplay, activeDrawable, blueGc, tick, 2, CoordModeOrigin);
	      sprintf(scratchString, "%d", i);
	      nChars = strlen(scratchString);
	      XDrawImageString(myDisplay, activeDrawable, labelGc,
			       tick[1].x - stringWidth(scratchString)/2, tick[1].y+charHeight+1, 
			       scratchString, nChars);
	      tick[0].y = plotY0 + 2;
	      tick[1].y = tick[0].y-5;
	      XDrawLines(myDisplay, activeDrawable, blueGc, tick, 2, CoordModeOrigin);
	    }
	    
	    log2nSWARMPixels = log((float)sWARMChunkWidth)/log(2.0);
	    if (userSelectedSWARMAverage > 0)
	      nSWARMChannelsToDisplay = N_SWARM_CHANNELS/userSelectedSWARMAverage;
	    else
	      nSWARMChannelsToDisplay = 1 << (log2nSWARMPixels+2);
	    if (nSWARMChannelsToDisplay < 4)
	      nSWARMChannelsToDisplay = 4;
	    if (nSWARMChannelsToDisplay > N_SWARM_CHANNELS)
	      nSWARMChannelsToDisplay = N_SWARM_CHANNELS;
	    nSWARMChannelsToDisplay = N_SWARM_CHANNELS/8;
	    nAveraged = N_SWARM_CHANNELS / nSWARMChannelsToDisplay;
	    if (sWARMChunkWidth > stringWidth("16384 Channels, averaged in groups of 8 for this plot"))
	      sprintf(scratchString, "16384 Channels, averaged in groups of %d for this plot", nAveraged);
	    else if (sWARMChunkWidth > stringWidth("4 channels averaged per point"))
	      sprintf(scratchString, "%d channels averaged per point", nAveraged);
	    else
	      sprintf(scratchString, "%d Chan. Ave'd", nAveraged);
	    nChars = strlen(scratchString);
	    XDrawImageString(myDisplay, activeDrawable, labelGc,
			     displayWidth/2 - stringWidth(scratchString)/2 - rightMargin/2,
			     displayHeight - 3, scratchString, nChars);
	    
	  } else
	    sWARMChunkWidth = (displayWidth-leftMargin-rightMargin)/SWARM_FRACTION-SWARM_OFFSET;
	  if (nSidebands == 1)
	    sWARMChunkHeight = baselineHeight;
	  else
	    sWARMChunkHeight = baselineHeight/2;
	  for (chunk = 0; chunk < 2; chunk++) {
	    if (!plotSWARMOnly || (chunkList[0] == chunk+49)) {
	      if (doubleBandwidth)
		sprintf(scratchString, "1 Receiver, 4 GHz Mode: Baselines %s, blocks %s, chunks %s, sidebands %s",
			bslnFilter, blockFilter, chunkFilter, sBFilter);
	      else
		sprintf(scratchString, "%s: Baselines %s, blocks %s, chunks %s, sidebands %s", rxFilter,
			bslnFilter, blockFilter, chunkFilter, sBFilter);
	      nChars = strlen(scratchString);
	      XDrawImageString(myDisplay, activeDrawable, labelGc,
			       displayWidth/2 - stringWidth(scratchString)/2 - rightMargin/2,
			       charHeight-4, scratchString, nChars);
	      if (sWARMChunkWidth > (stringWidth("SWARM s50") + 5))
		sprintf(scratchString, "SWARM s%d", 49+chunk);
	      else
		sprintf(scratchString, "s%d", 49+chunk);
	      nChars = strlen(scratchString);
	      XDrawImageString(myDisplay, activeDrawable, labelGc,
			       legacyEnd + chunksListed*sWARMChunkWidth + sWARMChunkWidth/2 - stringWidth(scratchString)/2,
			       charHeight*chunkLabelLine+8, scratchString, nChars);
	      lock_label("SWARM");
	      if (firstLabel) {
		lock_malloc(NULL);
		labelBase = (label *)malloc(sizeof(label));
		unlock_malloc(NULL);
		labelBase->next = (label *)NULL;
		firstLabel = FALSE;
		labelPtr = labelBase;
	      } else {
		nextLabel = labelBase;
		while (nextLabel != NULL) {
		  labelPtr = nextLabel;
		  nextLabel = (label *)labelPtr->next;
		}
	      }
	      lock_malloc(NULL);
	      labelPtr->next = (label *)malloc(sizeof(label));
	      unlock_malloc(NULL);
	      labelPtr = labelPtr->next;
	      labelPtr->next = NULL;
	      
	      labelPtr->tlcx = legacyEnd + chunksListed*sWARMChunkWidth; labelPtr->tlcy = charHeight*chunkLabelLine - 2;
	      labelPtr->blcx = labelPtr->tlcx;                                                   labelPtr->blcy = labelPtr->tlcy+charHeight + 4;
	      labelPtr->trcx = labelPtr->tlcx + sWARMChunkWidth;                  labelPtr->trcy = labelPtr->tlcy;
	      labelPtr->brcx = labelPtr->trcx;                                                   labelPtr->brcy = labelPtr->blcy;
	      labelPtr->block = SWARM_BLOCK;
	      labelPtr->chunk = 49+chunk;
	      labelPtr->ant1 = labelPtr->ant2 = -1;
	      labelPtr->iEf = LOW_RX_CODE;
	      labelPtr->sb = labelPtr->source = -1;
	      labelPtr->source = -1;
	      unlock_label("SWARM");
	      chunksListed++;
	    }
	  }
	  bsln = 0;
	  lock_cell("SWARM Cells");
	  log2nSWARMPixels = log((float)sWARMChunkWidth)/log(2.0);
	  if (userSelectedSWARMAverage > 0)
	    nSWARMChannelsToDisplay = N_SWARM_CHANNELS/userSelectedSWARMAverage;
	  else
	    nSWARMChannelsToDisplay = 1 << (log2nSWARMPixels+2);
	  if (nSWARMChannelsToDisplay < 4)
	    nSWARMChannelsToDisplay = 4;
	  if (nSWARMChannelsToDisplay > N_SWARM_CHANNELS)
	    nSWARMChannelsToDisplay = N_SWARM_CHANNELS;
	  nSWARMChannelsToDisplay = N_SWARM_CHANNELS / 8;
	  for (i = 0; i < nBsln; i++) {
	    int nChannelsToAverage, nPlotted;
	    float *ampPoints, *phaPoints, ampMax, ampMin, phaMax, phaMin, sWARMXScale, sWARMYScale;
	    XPoint box[5], data[nSWARMChannelsToDisplay];
	    
	    if (bslnPlottable(bsln2A1[bsln2Sorted[i]], bsln2A2[bsln2Sorted[i]])) {
	      chunksListed = 0;
	      for (chunk = 0; chunk < 2; chunk++) {
		if (!plotSWARMOnly || (chunkList[0] == chunk+49)) {
		  for (sb = 0; sb < 2; sb++) {
		    nPlotted = 0;
		    if ((sBFilter[0] == '*') || ((sb == 0) && (sBFilter[0] == 'L')) || ((sb == 1) && (sBFilter[0] == 'U'))) {
		      int j, goodData;
		      
		      /* Draw the little box that will show the spectrum */
		      box[0].x = box[4].x =  legacyEnd + chunksListed*sWARMChunkWidth + SWARM_OFFSET;
		      box[0].y = box[4].y = topMargin+bsln*(baselineSkip+baselineHeight)+1 + (nSidebands-1)*sb*sWARMChunkHeight;
		      box[1].x = box[0].x + sWARMChunkWidth;
		      box[1].y = box[0].y;
		      box[2].x = box[1].x;
		      box[2].y = box[1].y + sWARMChunkHeight;
		      box[3].y = box[2].y;
		      box[3].x = box[0].x;
		      XDrawLines(myDisplay, activeDrawable, blueGc, box, 5, CoordModeOrigin);
		      
		      /* Make the little box "clickable" */
		      if (firstCell) {
			lock_malloc(NULL);
			cellRoot = (cell *)malloc(sizeof(cell));
			unlock_malloc(NULL);
			cellRoot->next = NULL;
			firstCell = FALSE;
			cellPtr = cellRoot;
		      } else {
			cell *nextCell;
			
			nextCell = cellRoot;
			while (nextCell != NULL) {
			  cellPtr = nextCell;
			  nextCell = (cell *)cellPtr->next;
			}
			lock_malloc(NULL);
			cellPtr->next = (cell *)malloc(sizeof(cell));
			unlock_malloc(NULL);
			cellPtr = cellPtr->next;
			cellPtr->next = NULL;
		      }
		      cellPtr->tlcx = box[0].x;
		      cellPtr->tlcy = box[0].y;
		      cellPtr->trcx = box[1].x;
		      cellPtr->trcy = box[1].y;
		      cellPtr->blcx = box[3].x;
		      cellPtr->blcy = box[3].y;
		      cellPtr->brcx = box[2].x;
		      cellPtr->brcy = box[2].y;
		      cellPtr->iEf = 10+chunk;
		      cellPtr->ant1 = bsln2A1[bsln2Sorted[i]];
		      cellPtr->ant2 = bsln2A2[bsln2Sorted[i]];
		      cellPtr->sb = sb;
		      cellPtr->source = -1;
		      cellPtr->block = SWARM_BLOCK;
		      cellPtr->chunk = chunk;
		      
		      /* OK, now try to plot the SWARM data */
		      corrBsln = corrBaselineMapping[bsln2A1[bsln2Sorted[i]]-1][bsln2A2[bsln2Sorted[i]]-1];
		      goodData = TRUE;
		      if (!correlator.sWARMBaseline[corrBsln].haveCrossData) {
			sprintf(scratchString, "No Data");
			XDrawImageString(myDisplay, activeDrawable, yellowGc, (box[0].x+box[1].x)/2 - stringWidth(scratchString)/2
					 , (box[0].y + box[2].y)/2 + 6, scratchString, strlen(scratchString));		    
			goodData = FALSE;
		      }
		      if (((correlator.sWARMBaseline[corrBsln].ant[0] != bsln2A1[bsln2Sorted[i]])
			   || (correlator.sWARMBaseline[corrBsln].ant[1] != bsln2A2[bsln2Sorted[i]])) && 1) {
			fprintf(stderr, "SWARM antenna mismatch in correlator data structure for bsln %d %d-%d != %d-%d - won't plot\n",
				corrBsln, bsln2A1[bsln2Sorted[i]], bsln2A2[bsln2Sorted[i]], 
				correlator.sWARMBaseline[corrBsln].ant[0], correlator.sWARMBaseline[corrBsln].ant[1]);
			goodData = FALSE;
		      }
		      ampPoints = (float *)malloc(N_SWARM_CHANNELS*sizeof(float));
		      if (ampPoints == NULL) {
			perror("SWARM Amp malloc");
			break;
		      }
		      phaPoints = (float *)malloc(N_SWARM_CHANNELS*sizeof(float));
		      if (phaPoints == NULL) {
			perror("SWARM Amp malloc");
			break;
		      }
		      for (j = 0; j < N_SWARM_CHANNELS; j++) {
			ampPoints[j] = correlator.sWARMBaseline[corrBsln].amp[chunk][sb][j];
			phaPoints[j] = correlator.sWARMBaseline[corrBsln].phase[chunk][sb][j];
		      }
		      if (nSWARMChannelsToDisplay < N_SWARM_CHANNELS) {
			int ii, jj;
			float realAve, imagAve;
			
			nChannelsToAverage = N_SWARM_CHANNELS/nSWARMChannelsToDisplay;
			for (ii = 0; ii < nSWARMChannelsToDisplay; ii++) {
			  realAve = imagAve = 0.0;
			  for (jj = 0; jj < nChannelsToAverage; jj++) {
			    realAve += ampPoints[nChannelsToAverage*ii + jj]*cos(phaPoints[nChannelsToAverage*ii + jj]);
			    imagAve += ampPoints[nChannelsToAverage*ii + jj]*sin(phaPoints[nChannelsToAverage*ii + jj]);
			  }
			  ampPoints[ii] = sqrt(realAve*realAve + imagAve*imagAve);
			  phaPoints[ii] = atan2(imagAve, realAve);
			  if (!nANPattern[ii % 8])
			    ampPoints[ii] = phaPoints[ii] = NAN;
			}
		      }
		      ampMax = phaMax = -1.0e30; ampMin = phaMin = 1.0e30;
		      for (j = 1; j < nSWARMChannelsToDisplay; j++) {
			phaPoints[j] *= -1.0;
			if (ampPoints[j] > ampMax)
			  ampMax = ampPoints[j];
			if (ampPoints[j] < ampMin)
			  ampMin = ampPoints[j];
			if (phaPoints[j] > phaMax)
			  phaMax = phaPoints[j];
			if (phaPoints[j] < phaMin)
			  phaMin = phaPoints[j];
		      }
		      if (showAmp && goodData) {
			sWARMXScale = sWARMChunkWidth/((float)nSWARMChannelsToDisplay);
			if (ampMax != ampMin)
			  sWARMYScale = (sWARMChunkHeight-2)/(ampMax-ampMin);
			else {
			  printf("Aborting for chunk %d, because of DC Data\n", chunk);
			  sprintf(scratchString, "(DC Amp Data)");
			  XDrawImageString(myDisplay, activeDrawable, yellowGc, (box[0].x+box[1].x)/2 - stringWidth(scratchString)/2
					   , (box[0].y + box[2].y)/2 + 5, scratchString, strlen(scratchString));
			  break;
			}
			for (j = 0; j < nSWARMChannelsToDisplay; j++) {
			  if (nANPattern[j % 8]) {
			    data[nPlotted].x = box[0].x + ((float)j)*sWARMXScale;
			    data[nPlotted++].y = box[1].y - ampPoints[j]*sWARMYScale + ampMin*sWARMYScale + sWARMChunkHeight;
			  }
			}
			XDrawLines(myDisplay, activeDrawable, blueGc, data, nPlotted,
				   CoordModeOrigin);
		      }
		      nPlotted = 0;
		      if (showPhase && goodData) {
			if (!autoscalePhase) {
			  phaMin = -M_PI;
			  phaMax = M_PI;
			}
			sWARMXScale = sWARMChunkWidth/((float)nSWARMChannelsToDisplay);
			if (phaMax != phaMin)
			  sWARMYScale = (sWARMChunkHeight-2)/(phaMax-phaMin);
			else {
			  printf("Aborting for chunk %d, because of DC Data\n", chunk);
			  sprintf(scratchString, "(DC Phase Data)");
			  XDrawImageString(myDisplay, activeDrawable, yellowGc, (box[0].x+box[1].x)/2 - stringWidth(scratchString)/2
					   , (box[0].y + box[2].y)/2 + 5, scratchString, strlen(scratchString));
			  break;
			}
			for (j = 0; j < nSWARMChannelsToDisplay; j++) {
			  if (nANPattern[j % 8]) {
			    data[nPlotted].x = box[0].x + ((float)j)*sWARMXScale;
			    data[nPlotted++].y = box[1].y + phaPoints[j]*sWARMYScale - phaMin*sWARMYScale + 1;
			  }
			}
			XDrawPoints(myDisplay, activeDrawable, whiteGc, data, nPlotted,
				    CoordModeOrigin);
		      }
		      free(ampPoints); free(phaPoints);
		    }
		  }
		  chunksListed++;
		}
	      }
	      bsln++;
	    }
	  }
	  unlock_cell("SWARM Cells");
	} else { /* SWARM Zoomed */
	  int chunk, i, j, ant1, ant2, plotWidth, plotHeight, plotX0, plotY0, corrBsln, sb, maxAmpChan, minAmpChan,
	    maxPhaChan, minPhaChan, nSWARMChannels;
	  float xScale, yScale, *ampPoints, *phaPoints;
	  float ampMax, phaMax, ampMin, phaMin;
	  char scratchString[200];
	  XPoint data[N_SWARM_CHANNELS], pData[N_SWARM_CHANNELS];
	  int pltCount = 0;
	  int nANCount = 0;
	  int minX, maxX;
	  int shouldPlot[N_SWARM_CHANNELS];
	  
	  if (sWARMZoomedMin == sWARMZoomedMax) {
	    minX = 0;
	    maxX = N_SWARM_CHANNELS-1;
	  } else {
	    minX = sWARMZoomedMin;
	    maxX = sWARMZoomedMax;
	  }
	  nSWARMChannels = maxX - minX + 1;
	  /* Zoomed SWARM chunk plot */
	  chunk = chunkList[0];
	  for (i = 1; i < 8; i++)
	    for (j = i+1; j <= 8; j++)
	      if (requestedBaselines[i][j]) {
		ant1 = i;
		ant2 = j;
	      }
	  corrBsln = corrBaselineMapping[ant1-1][ant2-1];
	  sb = sBList[0];
	  sprintf(scratchString, "Left-click mouse in plot to unzoom");
	  nChars = strlen(scratchString);
	  XDrawImageString(myDisplay, activeDrawable, greenGc,
			   1, charHeight*2-4, scratchString, nChars);
	  if (sb == 0)
	    sprintf(scratchString, "SWARM chunk %d LSB for baseline %d-%d", 49+chunk, ant1, ant2);
	  else
	    sprintf(scratchString, "SWARM chunk %d USB for baseline %d-%d", 49+chunk, ant1, ant2);
	  nChars = strlen(scratchString);
	  XDrawImageString(myDisplay, activeDrawable, labelGc,
			   displayWidth/2 - stringWidth(scratchString)/2 - rightMargin/2,
			   charHeight-4, scratchString, nChars);
	  if (!correlator.sWARMBaseline[corrBsln].haveCrossData) {
	    sprintf(scratchString, "There's no valid data for this chunk");
	    XDrawImageString(myDisplay, activeDrawable, yellowGc, displayWidth/2 - stringWidth(scratchString)/2,
			     displayHeight/2 + 6, scratchString, strlen(scratchString));		    
	  } else {
	    /* Draw box box for plot */
	    data[0].x = 5;                 data[0].y = 40;
	    data[1].x = displayWidth - 5;  data[1].y = data[0].y;
	    data[2].x = data[1].x;         data[2].y = displayHeight - 20;
	    data[3].x = data[0].x;         data[3].y = data[2].y;
	    data[4].x = data[0].x;         data[4].y = data[0].y;
	    XDrawLines(myDisplay, activeDrawable, blueGc, data, 5, CoordModeOrigin);
	    plotWidth = data[1].x - data[0].x - 2; plotHeight = data[2].y - data[0].y - 2;
	    plotX0 = data[0].x; plotY0 = data[0].y;
	    xScale = ((float)plotWidth)/((float) nSWARMChannels);
	    for (i = minX; i < maxX; i += 1000) {
	      data[0].x = plotX0+(int)((i-minX)*xScale) + 1; data[0].y = plotY0+plotHeight + 2;
	      data[1].x = data[0].x;                         data[1].y = data[0].y+5;
	      XDrawLines(myDisplay, activeDrawable, blueGc, data, 2, CoordModeOrigin);
	      sprintf(scratchString, "%d", i);
	      nChars = strlen(scratchString);
	      XDrawImageString(myDisplay, activeDrawable, labelGc,
			       data[1].x - stringWidth(scratchString)/2, data[1].y+charHeight+1, 
			       scratchString, nChars);
	      data[0].y = plotY0;
	      data[1].y = data[0].y-5;
	      XDrawLines(myDisplay, activeDrawable, blueGc, data, 2, CoordModeOrigin);
	      data[0].y = 40;
	      data[1].y = displayHeight - 20;
	      XDrawLines(myDisplay, activeDrawable, darkGreyGc, data, 2, CoordModeOrigin);
	    }
	    ampPoints = (float *)malloc(N_SWARM_CHANNELS*sizeof(float));
	    if (ampPoints == NULL) {
	      perror("SWARM Amp malloc (z)");
	    }
	    phaPoints = (float *)malloc(N_SWARM_CHANNELS*sizeof(float));
	    if (phaPoints == NULL) {
	      perror("SWARM Amp malloc (z)");
	    }
	    for (i = minX; i < maxX; i++) {
	      ampPoints[i] = -correlator.sWARMBaseline[corrBsln].amp[chunk][sb][i];
	      phaPoints[i] = -correlator.sWARMBaseline[corrBsln].phase[chunk][sb][i];
	    }
	    ampMax = phaMax = -1.0e30; ampMin = phaMin = 1.0e30;
	    nANCount = pltCount = 0;
	    for (i = minX; i < maxX; i++) {
	      if (i > 0) {
		if (isnan(ampPoints[i])) {
		  shouldPlot[i] = FALSE;
		  nANCount++;
		} else {
		  shouldPlot[i] = TRUE;
		  if (ampPoints[i] > ampMax) {
		    ampMax = ampPoints[i];
		    maxAmpChan = i;
		  }
		  if (ampPoints[i] < ampMin) {
		    ampMin = ampPoints[i];
		    minAmpChan = i;
		  }
		  if (phaPoints[i] > phaMax) {
		    phaMax = phaPoints[i];
		    maxPhaChan = i;
		  }
		  if (phaPoints[i] < phaMin) {
		    phaMin = phaPoints[i];
		    minPhaChan = i;
		  }
		}
	      }
	    }
	    if (!autoscalePhase) {
	      phaMin = -M_PI;
	      phaMax = M_PI;
	    }
	    if (showAmp) {
	      xScale = plotWidth/((float)nSWARMChannels);
	      if (ampMax != ampMin)
		yScale = (plotHeight-2)/(ampMax-ampMin);
	      else {
		printf("Aborting for chunk %d, because of DC Data\n", chunk);
		sprintf(scratchString, "(DC Amp Data)");
		XDrawImageString(myDisplay, activeDrawable, yellowGc, (plotWidth - stringWidth(scratchString))/2,
				 plotHeight/2, scratchString, strlen(scratchString));
	      }
	      for (i = minX; i < maxX; i++) {
		data[i].x = plotX0 + ((float)(i-minX))*xScale + 1;
		data[i].y = plotY0 + ampPoints[i]*yScale - ampMin*yScale + 3;
		if (shouldPlot[i]) {
		  pData[pltCount].x = plotX0 + ((float)(i-minX))*xScale + 1;
		  pData[pltCount++].y = plotY0 + ampPoints[i]*yScale - ampMin*yScale + 3;
		}
	      }
	      XDrawLines(myDisplay, activeDrawable, blueGc, pData, pltCount, CoordModeOrigin);
	      sprintf(scratchString, "Minimum amp: %e at channel %d Maximum amp: %e at channel %d", -ampMax, maxAmpChan, -ampMin, minAmpChan);
	      nChars = strlen(scratchString);
	      XDrawImageString(myDisplay, activeDrawable, labelGc,
			       displayWidth/2 - stringWidth(scratchString)/2 - rightMargin/2,
			       2*charHeight - 4, scratchString, nChars);
	    }
	    if (showPhase) {
	      xScale = plotWidth/((float)nSWARMChannels);
	      if (phaMax != phaMin)
		yScale = (plotHeight-2)/(phaMax-phaMin);
	      else {
		printf("Aborting for chunk %d, because of DC Data\n", chunk);
		sprintf(scratchString, "(DC Phase Data)");
		XDrawImageString(myDisplay, activeDrawable, yellowGc, (plotWidth - stringWidth(scratchString))/2,
				 plotHeight/2, scratchString, strlen(scratchString));
	      }
	      pltCount = 0;
	      for (i = minX; i < maxX; i++) {
		data[i].x = plotX0 + ((float)(i-minX))*xScale;
		data[i].y = plotY0 + phaPoints[i]*yScale - phaMin*yScale + 2;
		if (shouldPlot[i]) {
		  pData[pltCount].x = plotX0 + ((float)(i-minX))*xScale;
		  pData[pltCount++].y = plotY0 + phaPoints[i]*yScale - phaMin*yScale + 2;
		}
	      }
	      XDrawPoints(myDisplay, activeDrawable, whiteGc, pData, pltCount,
			  CoordModeOrigin);
	      sprintf(scratchString, "Minimum phase: %0.1f (degrees) at channel %d Maximum phase: %0.1f at channel %d",
		      -phaMax*180.0/M_PI, maxPhaChan, -phaMin*180.0/M_PI, minPhaChan);
	      nChars = strlen(scratchString);
	      XDrawImageString(myDisplay, activeDrawable, labelGc,
			       displayWidth/2 - stringWidth(scratchString)/2 - rightMargin/2,
			       3*charHeight - 4, scratchString, nChars);
	      if (nANCount > 0) {
		sprintf(scratchString, "%d (%d%%) NANs (not plotted) NAN Pattern: %s", nANCount, (int)((((float)(100*nANCount))/((float)nSWARMChannels)) + 0.5), sWARMNANPatternString);
		XDrawImageString(myDisplay, activeDrawable, redGc,
				 displayWidth/2 - stringWidth(scratchString)/2 - rightMargin/2, 60,
				 scratchString, strlen(scratchString));
	      }
	    }
	    free(phaPoints);
	    free(ampPoints);
	  }
	}
      } /* End of SWARM chunk plotting stuff */
    } /* Ends the else condition corresponding to data being available to plot */
  } /* End of not autoCorrMode */
  if (!showRefresh)
    XCopyArea(myDisplay, pixmap, myWindow, whiteGc, 0, 0, displayWidth, displayHeight, 0, 0);
  unlock_data();
  unlock_X_display(TRUE);
  drawnOnce = TRUE;
}

void redrawScreenTrack()
{
  int iii, i, j, k, width, charHeight, plotHeight, plotWidth, plotXSkip, nCellsPlotted;
  int sBOffset, sourceCounter;
  int nScans = 0;
  int totalBaselines = 0;
  int bslnExists[11][11];
  int closureMap[11];
  int closureTriangle[136][3];
  int closureMapping[136][3];
  int nClosures;
  int sortOrder[57];
  int firstCell = TRUE;
  int firstLabel = TRUE;
  float sinLat, cosLat;
  char fileName[1000];
  cell *cellPtr;
  label *labelPtr;
  FILE *dataFile;
  plotLine *dataRoot = NULL;
  plotLine *nextLine, *ptr, *lastPtr;
  
  if (helpScreenActive)
    return;
  inRedrawScreenTrack = TRUE;
  getAntennaList(antennaInArray);
  lock_track("redrawScreenTrack()");
  cosLat = cos(LATITUDE);
  sinLat = sin(LATITUDE);
  sayRedrawing();
  if (!gotBslnLength)
    getBslnLength();
  redrawAbort = FALSE;
  for (i = 0; i < 11; i++)
    for (j = 0; j < 11; j++)
      bslnExists[i][j] = 0;
  sprintf(fileName, "%s/plot_me_5_rx%d", trackDirectory, 1-activeRx);
  dataFile = fopen(fileName, "r");
  if (dataFile == NULL) {
    sprintf(fileName, "%s/plot_me_4_rx%d", trackDirectory, 1-activeRx);
    dataFile = fopen(fileName, "r");
    if (dataFile == NULL) {
      sprintf(fileName, "%s/plot_me_3_rx%d", trackDirectory, 1-activeRx);
      dataFile = fopen(fileName, "r");
      if (dataFile == NULL) {
	sprintf(fileName, "%s/plot_me", trackDirectory);
	dataFile = fopen(fileName, "r");
	if (dataFile != NULL)
	  trackFileVersion = 1;
      } else
	trackFileVersion = 2;
    } else
      trackFileVersion = 3;
  } else
    trackFileVersion = 4;
  lock_X_display();
  if ((trackFileVersion == -1) || (dataFile == NULL)) {
    int nChars;
    char message[1000];
    
    sayNoData(2);
    if (activeRx == LOW_RX_CODE)
      sprintf(message, "Low Frequency Receiver Data in %s", trackDirectory);
    else
      sprintf(message, "High Frequency Receiver Data in %s", trackDirectory);
    nChars = strlen(message);
    XDrawImageString(myDisplay, activeDrawable, labelGc,
		     displayWidth/2 - stringWidth(message)/2 - rightMargin/2,
		     10,
		     message, nChars);
  } else {
    if (fieldSize == 1) {
      charHeight = bigFontStruc->max_bounds.ascent +
	bigFontStruc->max_bounds.descent;
    } else {
      charHeight = smallFontStruc->max_bounds.ascent +
	smallFontStruc->max_bounds.descent;
    }
    charHeight -= 4;
    if (showRefresh) {
      activeDrawable = myWindow;
      XClearWindow(myDisplay, myWindow);
    } else {
      activeDrawable = pixmap;
      XFillRectangle(myDisplay, pixmap, blackGc, 0, 0, displayWidth, displayHeight);
    }
    if (dataFile == NULL) {
      char errorMessage[1000];
      
      sprintf(errorMessage, "Can not find data in directory \"%s\"",
	      trackDirectory);
      width = stringWidth(errorMessage);
      XDrawImageString(myDisplay, activeDrawable, redGc,
		       displayWidth/2 - width/2,
		       displayHeight/2,
		       errorMessage, strlen(errorMessage));
    } else {
      int nSources, nBaselines;
      int nPScans, sScan, eScan;
      int lineNumber = 0;
      int maxBaselines = 0;
      float uTCS, uTCE;
      float hAMax = -1.0e30;
      float hAMin = 1.0e30;
      char inLine[10000], lineCopy[10000];
      char *sourceList[500];
      
      if (trackFileVersion >= 2)
	XtVaSetValues(timePlotToggle, XmNsensitive, True, NULL);
      else
	XtVaSetValues(timePlotToggle, XmNsensitive, False, NULL);
      if (trackFileVersion >= 3)
	XtVaSetValues(hAPlotToggle, XmNsensitive, True, NULL);
      else
	XtVaSetValues(hAPlotToggle, XmNsensitive, False, NULL);
      XtVaSetValues(applySecantZToggle, XmNsensitive, True, NULL);
      XtVaSetValues(rangeButton, XmNsensitive, True, NULL);
      XtVaSetValues(timeRangeButton, XmNsensitive, True, NULL);
      nSources = 0;
      /*
	OK, you've got a valid data file - now read everything in to a
	gigantic structure for plotting.
      */
      while (!feof(dataFile)) {
	int lineParsed, ant1[90], ant2[90], flag[90];
	int inOrder, parseCount, doCounter, sourceType;
	float amp[90][2], phase[90][2], coh[90][2], uTC, hARad, decRad, freqGHz;
	char *sourceName, *cAnt1, *cAnt2, *cFlag, *cAmp, *cPhase, *cCoh, *cUTC, *cHA, *cDec, *cFreq, *cType, *cPol;
	char *lasts;
	char *glitch = "(glitches)";
	
	getLine(dataFile, inLine);
	strcpy(lineCopy, inLine);
	lineNumber++;
	if (!feof(dataFile)) {
	  sourceName = strtok_r(inLine, " ", &lasts);
	  if (!strcmp(sourceName, "1")) {
	    sourceName = glitch;
	    strcpy(inLine, lineCopy);
	    lasts = &inLine[0];
	  }
	  if (debugMessagesOn)
	    printf("Source: \"%s\"\n", sourceName);
	  if (trackFileVersion >= 2) {
	    cUTC = strtok_r(NULL, " ", &lasts);
	    sscanf(cUTC, "%f", &uTC);
	  }
	  if (trackFileVersion > 2) {
	    cHA = strtok_r(NULL, " ", &lasts);
	    sscanf(cHA, "%f", &hARad);
	    hARad *= M_PI/12.0;
	    if (hAMin > hARad)
	      hAMin = hARad;
	    if (hAMax < hARad)
	      hAMax = hARad;
	    cDec = strtok_r(NULL, " ", &lasts);
	    sscanf(cDec, "%f", &decRad);
	    cFreq = strtok_r(NULL, " ", &lasts);
	    sscanf(cFreq, "%f", &freqGHz);
	    cType = strtok_r(NULL, " ", &lasts);
	    sscanf(cType, "%d", &sourceType);
	    if (hAPlot) {
	      uTC = hARad * 12.0/M_PI;
	      while (uTC < -12.0)
		uTC += 24.0;
	      while (uTC > 12.0)
		uTC -= 24.0;
	    }
	  }
	  if (!(((startTime > 0.0) && (uTC < startTime)) ||
		((endTime > 0.0) && (uTC > endTime)))) {
	    lineParsed = FALSE;
	    nBaselines = 0;
	    parseCount = 0;
	    while (!lineParsed) {
	      if (parseCount++ > 10000) {
		fprintf(stderr, "parseCount loop counter overflowed  - exiting\n");
		exit(-1);
	      }
	      cAnt1 = strtok_r(NULL, " ", &lasts);
	      if (cAnt1 == NULL)
		lineParsed = TRUE;
	      else {
		cAnt2 = strtok_r(NULL, " ", &lasts);
		cFlag = strtok_r(NULL, " ", &lasts);
		cAmp = strtok_r(NULL, " ", &lasts);
		cPhase = strtok_r(NULL, " ", &lasts);
		cCoh = strtok_r(NULL, " ", &lasts);
		if ((cAnt2 == NULL) || (cFlag == NULL) ||
		    (cAmp == NULL) || (cPhase == NULL) || (cCoh == NULL)) {
		  if (trackFileVersion >= 4)
		    cPol = cAnt1;
		  lineParsed = TRUE;
		} else {
		  sscanf(cAnt1, "%d", &ant1[nBaselines]);
		  sscanf(cAnt2, "%d", &ant2[nBaselines]);
		  sscanf(cFlag, "%d", &flag[nBaselines]);
		  sscanf(cAmp, "%f", &amp[nBaselines][1]);
		  sscanf(cPhase, "%f", &phase[nBaselines][1]);
		  sscanf(cCoh, "%f", &coh[nBaselines][1]);
		  if (((coh[nBaselines][1]) > 1.0) ||
		      ((coh[nBaselines][1]) < -1.0))
		    coh[nBaselines][1] = 0.0;
		  nBaselines++;
		  if (debugMessagesOn)
		    printf("Baseline %d: %d-%d\n", nBaselines, ant1[nBaselines-1], ant2[nBaselines-1]);
		}
	      }
	    }
	    if ((nBaselines % 2) == 0) {
	      int tBaselines;
	      
	      tBaselines = nBaselines / 2;
	      for (i = 0; i < tBaselines; i++) {
		if ((ant1[i] != ant1[i+tBaselines]) ||
		    (ant2[i] != ant2[i+tBaselines])) {
		  fprintf(stderr,
			  "Error 1 on plot file(line %d): a1[0] %d a2[0] %d a1[1] %d a2[1] %d nB %d tB %d\n",
			  lineNumber,
			  ant1[0], ant2[0], ant1[1], ant2[1], nBaselines, tBaselines);
		  exit(-1);
		} else {
		  amp[i][0] = amp[i+tBaselines][1];
		  phase[i][0] = phase[i+tBaselines][1];
		  coh[i][0] = coh[i+tBaselines][1];
		}
	      }
	    }
	    nBaselines /= 2;
	    if (nBaselines > maxBaselines)
	      maxBaselines = nBaselines;
	    doCounter = 0;
	    do {
	      if (doCounter++ > 10000) {
		fprintf(stderr, "doCounter overflow - exiting\n");
		exit(-1);
	      }
	      inOrder = TRUE;
	      for (i = 0; i < nBaselines-1; i++)
		if (ant1[i] > ant1[i+1]) {
		  int tI;
		  float tF;
		  
		  tI = ant1[i+1];
		  ant1[i+1] = ant1[i];
		  ant1[i] = tI;
		  tI = ant2[i+1];
		  ant2[i+1] = ant2[i];
		  ant2[i] = tI;
		  tI = flag[i+1];
		  flag[i+1] = flag[i];
		  flag[i] = tI;
		  tF = amp[i+1][0];
		  amp[i+1][0] = amp[i][0];
		  amp[i][0] = tF;
		  tF = amp[i+1][1];
		  amp[i+1][1] = amp[i][1];
		  amp[i][1] = tF;
		  tF = phase[i+1][0];
		  phase[i+1][0] = phase[i][0];
		  phase[i][0] = tF;
		  tF = phase[i+1][1];
		  phase[i+1][1] = phase[i][1];
		  phase[i][1] = tF;
		  tF = coh[i+1][0];
		  coh[i+1][0] = coh[i][0];
		  coh[i][0] = tF;
		  tF = coh[i+1][1];
		  coh[i+1][1] = coh[i][1];
		  coh[i][1] = tF;
		  inOrder = FALSE;
		  break;
		} else if ((ant1[i] == ant1[i+1]) && (ant2[i] > ant2[i+1])) {
		  int tI;
		  float tF;
		  
		  tI = ant1[i+1];
		  ant1[i+1] = ant1[i];
		  ant1[i] = tI;
		  tI = ant2[i+1];
		  ant2[i+1] = ant2[i];
		  ant2[i] = tI;
		  tI = flag[i+1];
		  flag[i+1] = flag[i];
		  flag[i] = tI;
		  tF = amp[i+1][0];
		  amp[i+1][0] = amp[i][0];
		  amp[i][0] = tF;
		  tF = amp[i+1][1];
		  amp[i+1][1] = amp[i][1];
		  amp[i][1] = tF;
		  tF = phase[i+1][0];
		  phase[i+1][0] = phase[i][0];
		  phase[i][0] = tF;
		  tF = phase[i+1][1];
		  phase[i+1][1] = phase[i][1];
		  phase[i][1] = tF;
		  tF = coh[i+1][0];
		  coh[i+1][0] = coh[i][0];
		  coh[i][0] = tF;
		  tF = coh[i+1][1];
		  coh[i+1][1] = coh[i][1];
		  coh[i][1] = tF;
		  inOrder = FALSE;
		  break;
		}
	    } while (inOrder == FALSE);
	    for (i = 0; i < nBaselines; i++) {
	      bslnExists[ant1[i]][ant2[i]] = 1;
	      if (debugMessagesOn)
		printf("%d-%d: (%f,%f,%f) (%f,%f,%f)\n",
		       ant1[i], ant2[i],
		       amp[i][0], phase[i][0], coh[i][0],
		       amp[i][1], phase[i][1], coh[i][1]);
	    }
	    nScans++;
	    nextLine = (plotLine *)malloc(sizeof *nextLine);
	    if (nextLine == NULL) {
	      perror("initial nextLine malloc");
	      exit(-1);
	    }
	    nextLine->next = NULL;
	    if (trackFileVersion >= 4) {
	      sscanf(cPol, "%x", &(nextLine->polar));
	    } else
	      nextLine->polar = 0;
	    /*
	      Add this to the source list, if necessary.
	    */
	    if (nSources == 0) {
	      sourceList[0] = (char *)malloc(strlen(sourceName)+1);
	      if (sourceList[0] == NULL) {
		perror("sourceList[0]");
		exit(-1);
	      }
	      strcpy(sourceList[0], sourceName);
	      nextLine->sourceNumber = 0;
	      uTCS = uTC;
	      if ((selectedSourceType != -1) && (sourceType != selectedSourceType))
		blackListedSource[0] = TRUE;
	      nSources = 1;
	    } else {
	      int foundName, sourceNumber;
	      
	      foundName = FALSE;
	      sourceNumber = 0;
	      while ((!foundName) && (sourceNumber < nSources)) {
		if (!strcmp(sourceList[sourceNumber], sourceName))
		  foundName = TRUE;
		else
		  sourceNumber++;
	      }
	      if (foundName)
		nextLine->sourceNumber = sourceNumber;
	      else {
		sourceList[nSources] = (char *)malloc(strlen(sourceName)+1);
		if (sourceList[nSources] == NULL) {
		  perror("sourceList[nSources]");
		  exit(-1);
		}
		strcpy(sourceList[nSources], sourceName);
		nextLine->sourceNumber = nSources;
		if ((selectedSourceType != -1) && (sourceType != selectedSourceType))
		  blackListedSource[nSources] = TRUE;
		nSources++;
	      }
	    }
	    
	    if (!hAPlot)
	      if (uTCS > uTC)
		uTC += 24.0;
	    if ((uTC - uTCS) < 2.0)
	      uTCE = uTC + (1.0/120.0);
	    else
	      uTCE = uTC + (uTC-uTCS)*0.005;
	    if (trackFileVersion >= 2)
	      nextLine->uTC = uTC;
	    else
	      nextLine->uTC = 0.0;
	    if (trackFileVersion > 3)
	      nextLine->sourceType = sourceType;
	    if ((trackFileVersion >= 3) && applySecantZ) {
	      nextLine->hA = hARad;
	      nextLine->el = asin(sinLat*sin(decRad) + cosLat*cos(decRad)*cos(hARad));
	    } else {
	      nextLine->hA = 0.0;
	      nextLine->el = M_PI/2.0;
	    }
	    nextLine->nBaselines = nBaselines;
	    nextLine->bsln = (bslnEntry *)malloc(nBaselines * sizeof(bslnEntry));
	    if (nextLine->bsln == NULL) {
	      perror("nextLine->bslns");
	      exit(-1);
	    }
	    for (i = 0; i < nBaselines; i++) {
	      nextLine->bsln[i].ant1 = ant1[i];
	      nextLine->bsln[i].ant2 = ant2[i];
	      if (gotBslnLength)
		nextLine->bsln[i].length = bslnLength[ant1[i]][ant2[i]];
	      else
		nextLine->bsln[i].length = 0.0;
	      nextLine->bsln[i].flag = flag[i];
	      nextLine->bsln[i].amp[0] = amp[i][0];
	      nextLine->bsln[i].amp[1] = amp[i][1];
	      nextLine->bsln[i].phase[0] = phase[i][0];
	      nextLine->bsln[i].phase[1] = phase[i][1];
	      nextLine->bsln[i].coh[0] = coh[i][0];
	      if (fabs(nextLine->bsln[i].coh[0]) > 1.0)
		printf("Found a wacky 0 coh = %f at i = %d\n",
		       nextLine->bsln[i].coh[0], i);
	      nextLine->bsln[i].coh[1] = coh[i][1];
	      if (fabs(nextLine->bsln[i].coh[1]) > 1.0)
		printf("Found a wacky 1 coh = %f at i = %d\n",
		       nextLine->bsln[i].coh[1], i);
	    }
	    if (dataRoot == NULL) {
	      dataRoot = nextLine;
	    } else {
	      ptr = dataRoot;
	      while (ptr != NULL) {
		lastPtr = ptr;
		ptr = (plotLine *)ptr->next;
	      }
	      lastPtr->next = nextLine;
	    }
	  }
	}
      }
      fclose(dataFile);
      if (hAPlot) {
	uTCS = hAMin*12.0/M_PI;
	uTCE = hAMax*12.0/M_PI;
      }
      if (startScan > 0)
	sScan = startScan;
      else
	sScan = 0;
      if (endScan > 0)
	eScan = endScan;
      else
	eScan = nScans; 
      nPScans = 1 + eScan - sScan;
      /*
	Now we have a linked list of amp-phase-coherence data for each
	scan.   Determine some global properties:
      */
      if (showClosure) {
	int nbslns = 0;
	int ijk, a[12], a1, a2, a3, nAntennas;
	
	for (i = 0; i <= 10; i++)
	  a[i] = 0;
	for (i = 0; i <= 10; i++)
	  for (j = 0; j <= 10; j++)
	    if (bslnExists[i][j]) {
	      a[i] = 1; a[j] = 1;
	      nbslns++;
	    }
	nAntennas = (int)(((sqrt((double)(1+8*nbslns))+1.0)*0.5)+0.5);
	totalBaselines = nClosures = (nAntennas-1)*(nAntennas-2)/2;
	ijk = 0;
	i = closureBaseAnt;
	while (ijk < nAntennas) {
	  if (a[i++]) {
	    closureMap[ijk] = i-1;
	    ijk++;
	  }
	  if (i > 10)
	    i = 1;
	}
	i = 0;
	for (a1 = 0; a1 < (nAntennas-2); a1++)
	  for (a2 = a1+1; a2 < (nAntennas-1); a2++)
	    for (a3 = a2+1; a3 < nAntennas; a3++) {
	      closureTriangle[i][0] = closureMap[a1];
	      closureTriangle[i][1] = closureMap[a2];
	      closureTriangle[i][2] = closureMap[a3];
	      for (ijk = 0; ijk < nBaselines; ijk++)
		if ((dataRoot->bsln[ijk].ant1 == closureMap[a1]) && (dataRoot->bsln[ijk].ant2 == closureMap[a2]))
		  closureMapping[i][0] = ijk;
		else if ((dataRoot->bsln[ijk].ant2 == closureMap[a1]) && (dataRoot->bsln[ijk].ant1 == closureMap[a2]))
		  closureMapping[i][0] = ijk;
		else if ((dataRoot->bsln[ijk].ant1 == closureMap[a2]) && (dataRoot->bsln[ijk].ant2 == closureMap[a3]))
		  closureMapping[i][1] = ijk;
		else if ((dataRoot->bsln[ijk].ant2 == closureMap[a2]) && (dataRoot->bsln[ijk].ant1 == closureMap[a3]))
		  closureMapping[i][1] = ijk;
		else if ((dataRoot->bsln[ijk].ant1 == closureMap[a1]) && (dataRoot->bsln[ijk].ant2 == closureMap[a3]))
		  closureMapping[i][2] = ijk;
		else if ((dataRoot->bsln[ijk].ant2 == closureMap[a1]) && (dataRoot->bsln[ijk].ant1 == closureMap[a3]))
		  closureMapping[i][2] = ijk;
	      i++;
	    }
      } else /* Not showClosure */
	for (i = 0; i < 11; i++)
	  for (j = 0; j < 11; j++)
	    if (bslnExists[i][j] && requestedBaselines[i][j]) {
	      if (debugMessagesOn && 0)
		printf("Saw baseline %d-%d\n", i, j);
	    totalBaselines++;
	    }
      for (i = 0; i < 57; i++)
	sortOrder[i] = i;
      if ((gotBslnLength && bslnOrder) && (!showClosure)) {
	int sorted;
	
	sorted = FALSE;
	while (!sorted) {
	  sorted = TRUE;
	  for (i = 0; i < (totalBaselines-1); i++) {
	    if (nextLine->bsln[sortOrder[i]].length > nextLine->bsln[sortOrder[i+1]].length) {
	      int tSO;
	      
	      sorted = FALSE;
	      tSO = sortOrder[i+1];
	      sortOrder[i+1] = sortOrder[i];
	      sortOrder[i] = tSO;
	    }
	  }
	}
      }
      if (debugMessagesOn && 0) {
	printf("nScans = %d, total baselines = %d\n",
	       nScans, totalBaselines);
	printf("Source list:\n");
      }
      /*
	List sources at the top of the display
      */
      if ((nSources > 0) && (totalBaselines > 0) && (nScans > 0)) {
	int nameWidth, totalPlotHeight, bslnSkip, nBaselinesPlotted;
	char *shortFileName, fileNameString[100];
	XPoint *data;
	
	if (debugMessagesOn)
	  printf("In redrawScreenTrack - get rid of old cell list if any\n");
	lock_cell("100");
	if (cellRoot != NULL) {
	  cell *nextCell;
	  
	  nextCell = (cell *)cellRoot->next;
	  free(cellRoot);
	  while (nextCell != NULL) {
	    cellPtr = nextCell;
	    nextCell = (cell *)cellPtr->next;
	    free(cellPtr);
	  }
	  cellRoot = NULL;
	}
	unlock_cell("100");
	if (debugMessagesOn)
	  printf("In redrawScreenTrack - get rid of old label list if any\n");
	lock_label("100");
	if (labelBase != NULL) {
	  label *nextLabel;
	  
	  nextLabel = (label *)labelBase->next;
	  free(labelBase);
	  while (nextLabel != NULL) {
	    labelPtr = nextLabel;
	    nextLabel = (label *)labelPtr->next;
	    free(labelPtr);
	  }
	  labelBase = NULL;
	}
	unlock_label("100");
	shortFileName = strstr(trackDirectory, "mir_data/");
	if (shortFileName != NULL) {
	  char rxName[10];
	  
	  shortFileName += 9;
	  if (activeRx == HIGH_RX_CODE)
	    sprintf(rxName, "High");
	  else
	    sprintf(rxName, "Low");
	  sprintf(fileNameString, "%s: %s", rxName, shortFileName);
	  width = stringWidth(fileNameString);
	  nameWidth = (displayWidth - width - 30)/ nSources;
	  XDrawImageString(myDisplay, activeDrawable, labelGc,
			   displayWidth - width - 30,
			   charHeight-3,
			   fileNameString, strlen(fileNameString));
	} else
	  nameWidth = (displayWidth)/ nSources;
	i = 0;
	sourceCounter = 0;
	while (i < nSources) {
	  label *nextLabel;
	  
	  if (sourceCounter++ > 10000) {
	    fprintf(stderr, "sourceCounter overflow - exiting\n");
	    exit(-1);
	  }
	  if ((i >= nGcs) && (nGcs < N_COLORS)) {
	    gcArray[i] = XCreateGC(myDisplay, activeDrawable, 0, 0);
	    if (XAllocNamedColor(myDisplay, cmap, colorName[i], &colorArray[i],
				 &colorArray[i]) == 0)
	      fprintf(stderr,
		      "%s pixel value cannot be obtained from default colormap\r\n",
		      colorName[i]);
	    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, gcArray[i]);
	    XSetFont(myDisplay, gcArray[i], smallFontStruc->fid);
	    XSetBackground(myDisplay, gcArray[i], mybackground);
	    XSetForeground(myDisplay, gcArray[i], colorArray[i].pixel);
	    XCopyGC(myDisplay, yellowGc, 0, gcArray[i]);
	    nGcs++;
	  }
	  if (debugMessagesOn && 0)
	    printf("%d: %s\n", i, sourceList[i]);
	  width = stringWidth(sourceList[i]);
	  if (currentSource == -1) {
	    if (plotSource(i))
	      XDrawImageString(myDisplay, activeDrawable, gcArray[chooseColor(i)],
			       i*nameWidth + nameWidth/2 - width/2,
			       charHeight-3,
			       sourceList[i], strlen(sourceList[i]));
	    else
	      XDrawImageString(myDisplay, activeDrawable, greyGc,
			       i*nameWidth + nameWidth/2 - width/2,
			       charHeight-3,
			       sourceList[i], strlen(sourceList[i]));
	  } else {
	    if (i == currentSource)
	      XDrawImageString(myDisplay, activeDrawable, whiteGc,
			       i*nameWidth + nameWidth/2 - width/2,
			       charHeight-3,
			       sourceList[i], strlen(sourceList[i]));
	    else
	      XDrawImageString(myDisplay, activeDrawable, greyGc,
			       i*nameWidth + nameWidth/2 - width/2,
			       charHeight-3,
			       sourceList[i], strlen(sourceList[i]));
	  }
	  lock_label("109");
	  if (firstLabel) {
	    lock_malloc(NULL);
	    labelBase = (label *)malloc(sizeof(label));
	    unlock_malloc(NULL);
	    labelBase->next = NULL;
	    firstLabel = FALSE;
	    labelPtr = labelBase;
	  } else {
	    nextLabel = labelBase;
	    while (nextLabel != NULL) {
	      labelPtr = nextLabel;
	      nextLabel = (label *)labelPtr->next;
	    }
	    lock_malloc(NULL);
	    labelPtr->next = (label *)malloc(sizeof(label));
	    unlock_malloc(NULL);
	    labelPtr = labelPtr->next;
	    labelPtr->next = NULL;
	  }
	  labelPtr->tlcx = i*nameWidth + nameWidth/2 - width/2;
	  labelPtr->tlcy = 0;
	  labelPtr->trcy = labelPtr->tlcy;
	  labelPtr->trcx = labelPtr->tlcx + width + width/4;
	  labelPtr->blcx = labelPtr->tlcx;
	  labelPtr->blcy = labelPtr->tlcy + charHeight;
	  labelPtr->brcx = labelPtr->trcx;
	  labelPtr->brcy = labelPtr->blcy;
	  labelPtr->ant1 = -1;
	  labelPtr->ant2 = -1;
	  labelPtr->block = -1;
	  labelPtr->chunk = -1;
	  labelPtr->iEf = -1;
	  labelPtr->sb = -1;
	  labelPtr->source = i;
	  unlock_label("109");
	  i++;
	}
	if (sBFilter[0] == '*') {
	  sBOffset = 0;
	} else {
	  if (sBFilter[0] == 'L')
	    sBOffset = 1;
	  else
	    sBOffset = 0;
	}
	totalPlotHeight = displayHeight - 2*charHeight;
	bslnSkip = (int)((float)totalPlotHeight / (float)totalBaselines);
	if (nSidebands > 1)
	  plotHeight = (int)((float)totalPlotHeight / (float)(totalBaselines*nSidebands)) - 1;
	else
	  plotHeight = (int)((float)totalPlotHeight / (float)totalBaselines);
	if (showClosure)
	  plotXSkip = stringWidth("USBB  ");
	else
	  plotXSkip = stringWidth("USB  ");
	plotWidth = displayWidth - plotXSkip - 3;
	nBaselinesPlotted = nCellsPlotted = 0;
	for (iii = 0; nBaselinesPlotted < totalBaselines; iii++) {
	  i = sortOrder[iii];
	  if (requestedBaselines[dataRoot->bsln[i].ant1][dataRoot->bsln[i].ant2]) {
	    XPoint box[5];
	    label *nextLabel;
	    char bslnName[15];
	    
	    if (showClosure)
	      sprintf(bslnName, "%d:%d:%d", closureTriangle[iii][0], closureTriangle[iii][1], closureTriangle[iii][2]);
	    else
	      sprintf(bslnName, "%d-%d", nextLine->bsln[i].ant1,
		      nextLine->bsln[i].ant2);
	    width = stringWidth(bslnName)+5;
	    XDrawImageString(myDisplay, activeDrawable, labelGc,
			     0,
			     3 + charHeight + nBaselinesPlotted*bslnSkip + bslnSkip/2,
			     bslnName, strlen(bslnName));
	    lock_label("105");
	    nextLabel = labelBase;
	    while (nextLabel != NULL) {
	      labelPtr = nextLabel;
	      nextLabel = (label *)labelPtr->next;
	    }
	    lock_malloc(NULL);
	    labelPtr->next = (label *)malloc(sizeof(label));
	    unlock_malloc(NULL);
	    labelPtr = labelPtr->next;
	    labelPtr->next = NULL;
	    labelPtr->tlcx = 0;
	    labelPtr->tlcy = 3 + nBaselinesPlotted*bslnSkip + bslnSkip/2;
	    labelPtr->trcy = labelPtr->tlcy;
	    labelPtr->trcx = stringWidth("8-8");
	    labelPtr->blcx = 0;
	    labelPtr->blcy = labelPtr->tlcy + charHeight;
	    labelPtr->brcx = labelPtr->trcx;
	    labelPtr->brcy = labelPtr->blcy;
	    labelPtr->ant1 = nextLine->bsln[i].ant1;
	    labelPtr->ant2 = nextLine->bsln[i].ant2;
	    labelPtr->block = -1;
	    labelPtr->chunk = -1;
	    labelPtr->sb = -1;
	    labelPtr->iEf = -1;
	    labelPtr->source = -1;
	    unlock_label("105");
	    if ((bslnSkip > 3*charHeight) && (nSidebands == 2)) {
	      sprintf(bslnName, "USB");
	      width = stringWidth(bslnName);
	      XDrawImageString(myDisplay, activeDrawable, blueGc,
			       0,
			       3 + charHeight + nBaselinesPlotted*bslnSkip + bslnSkip/4,
			       bslnName, strlen(bslnName));
	      lock_label("106");
	      nextLabel = labelBase;
	      while (nextLabel != NULL) {
		labelPtr = nextLabel;
		nextLabel = (label *)labelPtr->next;
	      }
	      lock_malloc(NULL);
	      labelPtr->next = (label *)malloc(sizeof(label));
	      unlock_malloc(NULL);
	      labelPtr = labelPtr->next;
	      labelPtr->next = NULL;
	      labelPtr->tlcx = 0;
	      labelPtr->tlcy = 3 + nBaselinesPlotted*bslnSkip + bslnSkip/4;
	      labelPtr->trcy = labelPtr->tlcy;
	      labelPtr->trcx = stringWidth("USB");
	      labelPtr->blcx = 0;
	      labelPtr->blcy = labelPtr->tlcy + charHeight;
	      labelPtr->brcx = labelPtr->trcx;
	      labelPtr->brcy = labelPtr->blcy;
	      labelPtr->ant1 = -1;
	      labelPtr->block = -1;
	      labelPtr->chunk = -1;
	      labelPtr->iEf = -1;
	      labelPtr->sb = 1;
	      labelPtr->source = -1;
	      unlock_label("106");
	      sprintf(bslnName, "LSB");
	      width = stringWidth(bslnName);
	      XDrawImageString(myDisplay, activeDrawable, blueGc,
			       0, 3 + charHeight + nBaselinesPlotted*bslnSkip + bslnSkip - bslnSkip/4,
			       bslnName, strlen(bslnName));
	      lock_label("107");
	      nextLabel = labelBase;
	      while (nextLabel != NULL) {
		labelPtr = nextLabel;
		nextLabel = (label *)labelPtr->next;
	      }
	      lock_malloc(NULL);
	      labelPtr->next = (label *)malloc(sizeof(label));
	      unlock_malloc(NULL);
	      labelPtr = labelPtr->next;
	      labelPtr->next = NULL;
	      labelPtr->tlcx = 0;
	      labelPtr->tlcy = 3 + nBaselinesPlotted*bslnSkip + bslnSkip - bslnSkip/4;
	      labelPtr->trcy = labelPtr->tlcy;
	      labelPtr->trcx = stringWidth("LSB");
	      labelPtr->blcx = 0;
	      labelPtr->blcy = labelPtr->tlcy + charHeight;
	      labelPtr->brcx = labelPtr->trcx;
	      labelPtr->brcy = labelPtr->blcy;
	      labelPtr->ant1 = -1;
	      labelPtr->iEf = -1;
	      labelPtr->block = -1;
	      labelPtr->chunk = -1;
	      labelPtr->sb = 0;
	      labelPtr->source = -1;
	      unlock_label("107");
	    } else if ((nSidebands == 1) && (nBaselinesPlotted == 0)) {
	      if (sBFilter[0] == 'U')
		sprintf(bslnName, "USB");
	      else
		sprintf(bslnName, "LSB");
	      XDrawImageString(myDisplay, activeDrawable, blueGc,
			       0,
			       charHeight-3,
			       bslnName, strlen(bslnName));
	    }
	    /*
	      Draw plot boxes
	    */
	    for (j = 0; j < nSidebands; j++) {
	      box[0].x = box[4].x = box[3].x = plotXSkip;
	      box[0].y = box[4].y = charHeight + nBaselinesPlotted*bslnSkip + j*plotHeight;
	      box[1].x = box[2].x = box[0].x + plotWidth;
	      box[1].y = box[0].y;
	      box[2].y = box[3].y = box[1].y + plotHeight;
	      XDrawLines(myDisplay, activeDrawable, blueGc, box, 5,
			 CoordModeOrigin);
	      lock_cell("102");
	      if (firstCell) {
		lock_malloc(NULL);
		cellRoot = (cell *)malloc(sizeof(cell));
		unlock_malloc(NULL);
		cellRoot->next = (cell *)NULL;
		firstCell = FALSE;
		cellPtr = cellRoot;
	      } else {
		cell *nextCell;
		
		nextCell = cellRoot;
		while (nextCell != NULL) {
		  cellPtr = nextCell;
		  nextCell = (cell *)cellPtr->next;
		}
		lock_malloc(NULL);
		cellPtr->next = (cell *)malloc(sizeof(cell));
		unlock_malloc(NULL);
		cellPtr = cellPtr->next;
		cellPtr->next = NULL;
	      }
	      cellPtr->tlcx = box[0].x;
	      cellPtr->tlcy = box[0].y;
	      cellPtr->trcx = box[1].x;
	      cellPtr->trcy = box[1].y;
	      cellPtr->blcx = box[3].x;
	      cellPtr->blcy = box[3].y;
	      cellPtr->brcx = box[2].x;
	      cellPtr->brcy = box[2].y;
	      cellPtr->iEf = activeRx;
	      cellPtr->ant1 = nextLine->bsln[i].ant1;
	      cellPtr->ant2 = nextLine->bsln[i].ant2;
	      cellPtr->block = -1;
	      cellPtr->chunk = -1;
	      if (((sBList[j] != 0) && (nSidebands == 1)) ||
		  ((sBList[j] == 0) && (nSidebands > 1)))
		cellPtr->sb = 1;
	      else
		cellPtr->sb = 0;
	      cellPtr->source = -1;
	      unlock_cell("2");
	      nCellsPlotted++;
	    }
	    nBaselinesPlotted++;
	  }
	} /* for (iii ... */
	/*
	  Put ticks along the bottom box
	*/
	yTickTop = charHeight + (totalBaselines-1)*bslnSkip +
	  nSidebands*plotHeight - 3;
	xStartScan = plotXSkip;
	xStartScanNumber = sScan;
	xEndScanNumber = eScan;
	xEndScan = plotXSkip + (int)((float)plotWidth * (float)(nPScans-1)/(float)nPScans);
	xScanRange = plotWidth;
	if (nPScans > 10) {
	  int step;
	  XPoint tick[2];
	  char tickLabel[20];
	  cell *nextCell;
	  
	  nextCell = cellRoot;
	  while (nextCell != NULL) {
	    cellPtr = nextCell;
	    nextCell = (cell *)cellPtr->next;
	  }
	  lock_malloc(NULL);
	  cellPtr->next = (cell *)malloc(sizeof(cell));
	  unlock_malloc(NULL);
	  cellPtr = cellPtr->next;
	  cellPtr->next = NULL;
	  cellPtr->blcx = xStartScan;
	  cellPtr->blcy = displayHeight;
	  cellPtr->brcx = displayWidth;
	  cellPtr->brcy = displayHeight;
	  cellPtr->trcx = displayWidth;
	  cellPtr->trcy = charHeight + (totalBaselines-1)*bslnSkip +
		nSidebands*plotHeight - 3;
	  cellPtr->tlcx = xStartScan;
	  cellPtr->tlcy = charHeight + (totalBaselines-1)*bslnSkip +
		nSidebands*plotHeight - 3;
	  cellPtr->ant1 = -1;
	  cellPtr->ant2 = -1;
	  cellPtr->block = TIME_LABEL;
	  cellPtr->chunk = -1;
	  cellPtr->sb = -1;
	  cellPtr->source = -1;
	  unlock_cell("2");
	  nCellsPlotted++;
	  
	  if ((trackFileVersion < 2) || (!timePlot)) {
	    if (nPScans > 100)
	      step = 100;
	    else
	      step = 10;
	    for (i = sScan; i <= (nPScans+sScan); i += step) {
	      tick[0].x = tick[1].x =
		plotXSkip + (int)((float)(i-sScan)*(float)plotWidth / (float)nPScans);
	      tick[0].y = charHeight + (totalBaselines-1)*bslnSkip +
		nSidebands*plotHeight - 3;
	      tick[1].y = tick[0].y + 7;
	      if (grid)
		tick[0].y = charHeight;
	      XDrawLines(myDisplay, activeDrawable, blueGc, tick, 2,
			 CoordModeOrigin);
	      sprintf(tickLabel, "%d", i);
	      width = stringWidth(tickLabel);
	      if ((tick[0].x + width/2) < displayWidth)
		XDrawImageString(myDisplay, activeDrawable, labelGc,
				 tick[0].x - width/2,
				 tick[1].y + charHeight - 3,
				 tickLabel, strlen(tickLabel));
	    }
	    if (nPScans > 1) {
	      timeAxisM = (float)nPScans / (float)plotWidth;
	      timeAxisA = plotXSkip;
	      timeAxisB = sScan;
	    }
	  } else {
	    if (nPScans > 1) {
	      int hh, mm, lStep;
	      float tickTime;
	      char plotTimeString[20];

	      if ((uTCE - uTCS) <= 2.0)
		lStep = 10;
	      else if ((uTCE - uTCS) <= 4.0)
		lStep = 20;
	      else if ((uTCE - uTCS) <= 5.0)
		lStep = 30;
	      else
		lStep = 60;
	      /*
	      printf("Trying to draw ticks, start = %f, end = %f\n", uTCS, uTCE);
	      */
	      hh = (int)uTCS;
	      mm = (int)((uTCS - (float)hh)*60.0);
	      mm = lStep*(mm/lStep);
	      tickTime = (float)hh + ((float)mm) / 60.0;
	      timeAxisM = (uTCE - uTCS) / (float)plotWidth;
	      timeAxisA = plotXSkip;
	      timeAxisB = uTCS;
	      while (tickTime < uTCE) {
		if (tickTime >= uTCS) {
		  tick[0].x = tick[1].x =
		    plotXSkip + (int)((tickTime - uTCS)*(float)plotWidth / (uTCE - uTCS));
		  tick[0].y = charHeight + (totalBaselines-1)*bslnSkip +
		    nSidebands*plotHeight - 3;
		  tick[1].y = tick[0].y + 7;
		  if (grid)
		    tick[0].y = charHeight;
		  XDrawLines(myDisplay, activeDrawable, blueGc, tick, 2,
			     CoordModeOrigin);
		  sprintf(plotTimeString, "%02d:%02d      ", hh, abs(mm));
		  width = stringWidth(plotTimeString);
		  if ((tick[0].x + width/2) < displayWidth)
		    XDrawImageString(myDisplay, activeDrawable, labelGc,
				     tick[0].x - width/2,
				     tick[1].y + charHeight - 3,
				     plotTimeString, strlen(plotTimeString));
		}
		mm += lStep;
		if (mm >= 60) {
		  hh++;
		  mm = 0;
		}
		tickTime = (float)hh + ((float)mm) / 60.0;
	      }
	    } else {
	    }
	  }
	}
	if ((bslnFilter[0] != '*') ||
	    (sourceFilter[0] != '*') ||
	    (currentSource != -1) ||
	    (sBFilter[0] != '*') ||
	    (startScan >= 0) ||
	    (endScan >= 0)) {
	  label *nextLabel;
	  
	  XDrawImageString(myDisplay, activeDrawable, greenGc,
			   0,
			   displayHeight,
			   "RF", 2);
	  lock_label("104");
	  nextLabel = labelBase;
	  while (nextLabel != NULL) {
	    labelPtr = nextLabel;
	    nextLabel = (label *)labelPtr->next;
	  }
	  lock_malloc(NULL);
	  labelPtr->next = (label *)malloc(sizeof(label));
	  unlock_malloc(NULL);
	  labelPtr = labelPtr->next;
	  labelPtr->next = NULL;
	  labelPtr->tlcx = 0;
	  labelPtr->tlcy = displayHeight-charHeight;
	  labelPtr->trcy = labelPtr->tlcy;
	  labelPtr->trcx = stringWidth("RF");
	  labelPtr->blcx = 0;
	  labelPtr->blcy = displayHeight;
	  labelPtr->brcx = labelPtr->trcx;
	  labelPtr->brcy = labelPtr->blcy;
	  labelPtr->ant1 = 1000;
	  labelPtr->block = -1;
	  labelPtr->chunk = -1;
	  labelPtr->iEf = -1;
	  labelPtr->sb = -1;
	  labelPtr->source = -1;
	  unlock_label("104");
	}
	/*
	  Plot them scans
	*/
	if (showAmp) {
	  float ampMax, ampMin, yScale, ampScale;

	  data = (XPoint *)malloc(nPScans*sizeof(XPoint));
	  if (data == NULL) {
	    perror("malloc of XPoints for amp");
	    exit(-1);
	  }
	  if (!autoscaleAmplitude) {
	    /* Calculate a max and min amplitude for the entire data set */
	    ampMax = -1.0e50;
	    ampMin = 1.0e50;
	    nBaselinesPlotted = 0;
	    for (iii = 0; (nBaselinesPlotted < totalBaselines) && (!redrawAbort); iii++) {
	      i = sortOrder[iii];
	      if (requestedBaselines[dataRoot->bsln[i].ant1][dataRoot->bsln[i].ant2]) {
		for (j = 0; j < nSidebands; j++) {
		  int point;
		  
		  point = 0;
		  nextLine = dataRoot;
		  while (nextLine != NULL) {
		    if ((((currentSource == -1) || (currentSource == nextLine->sourceNumber)) &&
			 ((point >= sScan) && (point <= eScan)) && (((nextLine->bsln[i].flag > 0) && showGood) ||
								    ((nextLine->bsln[i].flag < 0) && showBad))) &&
			(plotSource(nextLine->sourceNumber)) &&
			(polarState(nextLine->bsln[i].ant1, nextLine->bsln[i].ant2, nextLine->polar) & polMask)) {
		      if (applySecantZ)
			ampScale = 1.0/sin(nextLine->el);
		      else
			ampScale = 1.0;
		      if (!logPlot) {
			if (nextLine->bsln[i].amp[j+sBOffset]*ampScale > ampMax)
			  ampMax = nextLine->bsln[i].amp[j+sBOffset]*ampScale;
			if (nextLine->bsln[i].amp[j+sBOffset]*ampScale < ampMin)
			  ampMin = nextLine->bsln[i].amp[j+sBOffset]*ampScale;
		      } else {
			if (log(nextLine->bsln[i].amp[j+sBOffset]*ampScale) > ampMax)
			  ampMax = log(nextLine->bsln[i].amp[j+sBOffset]*ampScale);
			if (log(nextLine->bsln[i].amp[j+sBOffset]*ampScale) < ampMin)
			  ampMin = log(nextLine->bsln[i].amp[j+sBOffset]*ampScale);
		      }
		    }
		    point++;
		    nextLine = (plotLine *)nextLine->next;
		  }
		}
		nBaselinesPlotted++;
	      }
	    }
	    if (plotFromZero && (!logPlot))
	      ampMin = 0.0;
	  }
	  nBaselinesPlotted = 0;
	  for (iii = 0; (nBaselinesPlotted < totalBaselines) && (!redrawAbort); iii++) {
	    i = sortOrder[iii];
	    if (requestedBaselines[dataRoot->bsln[i].ant1][dataRoot->bsln[i].ant2]) {
	      for (j = 0; j < nSidebands; j++) {
		int jj, sPoints, point;

		if (autoscaleAmplitude) {
		  ampMax = -1.0e50;
		  ampMin = 1.0e50;
		}
		point = 0;
		nextLine = dataRoot;
		while (nextLine != NULL) {
		  if ((((currentSource == -1) || (currentSource == nextLine->sourceNumber)) &&
		      ((point >= sScan) && (point <= eScan)) && (((nextLine->bsln[i].flag > 0) && showGood) ||
								 ((nextLine->bsln[i].flag < 0) && showBad))) &&
		      (plotSource(nextLine->sourceNumber)) &&
		      (polarState(nextLine->bsln[i].ant1, nextLine->bsln[i].ant2, nextLine->polar) & polMask)) {
		    if (applySecantZ)
		      ampScale = 1.0/sin(nextLine->el);
		    else
		      ampScale = 1.0;
		    if (autoscaleAmplitude) {
		      if (!logPlot) {
			if (nextLine->bsln[i].amp[j+sBOffset]*ampScale > ampMax)
			  ampMax = nextLine->bsln[i].amp[j+sBOffset]*ampScale;
			if (nextLine->bsln[i].amp[j+sBOffset]*ampScale < ampMin)
			  ampMin = nextLine->bsln[i].amp[j+sBOffset]*ampScale;
		      } else {
			if (log(nextLine->bsln[i].amp[j+sBOffset]*ampScale) > ampMax)
			  ampMax = log(nextLine->bsln[i].amp[j+sBOffset]*ampScale);
			if (log(nextLine->bsln[i].amp[j+sBOffset]*ampScale) < ampMin)
			  ampMin = log(nextLine->bsln[i].amp[j+sBOffset]*ampScale);
		      }
		    }
		  }
		  point++;
		  nextLine = (plotLine *)nextLine->next;
		}
		if (plotFromZero && (!logPlot))
		  ampMin = 0.0;
		yScale = ((float)plotHeight-4.0)/(ampMin-ampMax);
		for (jj = 0; jj < nSources; jj++) {
		  sPoints = point = 0;
		  nextLine = dataRoot;
		  for (k = 0; k < nScans; k++) {
		    if (((nextLine->sourceNumber == jj) || (showPhase)) &&
			(((nextLine->bsln[i].flag > 0) && showGood) ||
			 ((nextLine->bsln[i].flag < 0) && showBad))) {
		      if ((point >= sScan) && (point <= eScan)) {
			if (((currentSource == -1) || (currentSource == nextLine->sourceNumber)) &&
			    (plotSource(nextLine->sourceNumber)) &&
			    (polarState(nextLine->bsln[i].ant1, nextLine->bsln[i].ant2, nextLine->polar) & polMask)) {
			  if (applySecantZ)
			    ampScale = 1.0/sin(nextLine->el);
			  else
			    ampScale = 1.0;
			  if ((trackFileVersion < 2) || (!timePlot))
			    data[sPoints].x = plotXSkip +
			      (int)(((float)(k-sScan)) * ((float)plotWidth) / ((float)nPScans));
			  else
			    data[sPoints].x = plotXSkip +
			      (int)((nextLine->uTC - uTCS) * ((float)plotWidth) /
				    (uTCE - uTCS));
			  if (!logPlot)
			    data[sPoints++].y = plotHeight/2 + charHeight + nBaselinesPlotted*bslnSkip +
			      j*plotHeight +
			      (int)((nextLine->bsln[i].amp[j+sBOffset]*ampScale-ampMax) * yScale -
				    (float)((plotHeight-2)/2.0));
			  else
			    data[sPoints++].y = plotHeight/2 + charHeight + nBaselinesPlotted*bslnSkip +
			      j*plotHeight +
			      (int)((log(nextLine->bsln[i].amp[j+sBOffset]*ampScale)-ampMax) * yScale -
				    (float)((plotHeight-2)/2.0));
			}
		      }
		    }
		    point++;
		    nextLine = (plotLine *)nextLine->next;
		  }
		  if ((!showPhase) && (sPoints > 0) && (plotSource(jj)))
		    if (currentSource == -1)
		      XDrawPoints(myDisplay, activeDrawable, gcArray[chooseColor(jj)], data, sPoints,
				  CoordModeOrigin);
		    else
		      XDrawPoints(myDisplay, activeDrawable, gcArray[0], data, sPoints,
				  CoordModeOrigin);
		  else if (sPoints > 0) {
		    XDrawLines(myDisplay, activeDrawable, greyGc, data, sPoints,
			       CoordModeOrigin);
		    jj = nSources;
		  }
		}
	      }
	      nBaselinesPlotted++;
	    }
	  }
	  free(data);
	} else if (showCoh) {
	  int point;
	  float cohMax, cohMin, yScale;
	  
	  data = (XPoint *)malloc(nPScans*sizeof(XPoint));
	  if (data == NULL) {
	    perror("malloc of XPoints for coh");
	    exit(-1);
	  }
	  nBaselinesPlotted = 0;
	  for (iii = 0; (nBaselinesPlotted < totalBaselines) && (!redrawAbort); iii++) {
	    i = sortOrder[iii];
	    if (requestedBaselines[dataRoot->bsln[i].ant1][dataRoot->bsln[i].ant2]) {
	      for (j = 0; j < nSidebands; j++) {
		int jj, sPoints;
		
		cohMax = -1.0e50;
		cohMin = 1.0e50;
		point = 0;
		nextLine = dataRoot;
		while (nextLine != NULL) {
		  if ((((currentSource == -1) || (currentSource == nextLine->sourceNumber)) &&
		       ((point >= sScan) && (point <= eScan))) && (plotSource(currentSource)) &&
		      (polarState(nextLine->bsln[i].ant1, nextLine->bsln[i].ant2, nextLine->polar) & polMask)) {
		    if (!logPlot) {
		      if (nextLine->bsln[i].coh[j+sBOffset] > cohMax)
			cohMax = nextLine->bsln[i].coh[j+sBOffset];
		      if (nextLine->bsln[i].coh[j+sBOffset] < cohMin)
			cohMin = nextLine->bsln[i].coh[j+sBOffset];
		    } else {
		      if (log(nextLine->bsln[i].coh[j+sBOffset]) > cohMax)
			cohMax = log(nextLine->bsln[i].coh[j+sBOffset]);
		      if (log(nextLine->bsln[i].coh[j+sBOffset]) < cohMin)
			cohMin = log(nextLine->bsln[i].coh[j+sBOffset]);
		    }
		  }
		  point++;
		  nextLine = (plotLine *)nextLine->next;
		}
		if (plotFromZero && (!logPlot))
		  cohMin = 0.0;
		if (plotToOne) {
		  if (logPlot)
		    cohMax = 0.0;
		  else
		    cohMax = 1.0;
		}
		yScale = ((float)plotHeight-4.0)/(cohMin-cohMax);
		for (jj = 0; jj < nSources; jj++) {
		  sPoints = 0;
		  nextLine = dataRoot;
		  point = 0;
		  for (k = 0; k < nScans; k++) {
		    if (((nextLine->sourceNumber == jj) || (showPhase)) &&
			(((nextLine->bsln[i].flag > 0) && showGood) ||
			 ((nextLine->bsln[i].flag < 0) && showBad))) {
		      if ((point >= sScan) && (point <= eScan)) {
			if (((currentSource == -1) || (currentSource == nextLine->sourceNumber)) &&
			    (plotSource(currentSource)) &&
			    (polarState(nextLine->bsln[i].ant1, nextLine->bsln[i].ant2, nextLine->polar) & polMask)) {
			  if ((trackFileVersion < 2) || (!timePlot))
			    data[sPoints].x = plotXSkip +
			      (int)(((float)(k-sScan)) * ((float)plotWidth) / ((float)nPScans));
			  else
			    data[sPoints].x = plotXSkip +
			      (int)((nextLine->uTC - uTCS) * ((float)plotWidth) /
				    (uTCE - uTCS));
			  if (!logPlot) {
			    data[sPoints++].y = plotHeight/2 + charHeight + nBaselinesPlotted*bslnSkip +
			      j*plotHeight +
			      (int)((nextLine->bsln[i].coh[j+sBOffset]-cohMax) * yScale -
				    (float)((plotHeight-2)/2.0));
			  } else
			    data[sPoints++].y = plotHeight/2 + charHeight + nBaselinesPlotted*bslnSkip +
			      j*plotHeight +
			      (int)((log(nextLine->bsln[i].coh[j+sBOffset])-cohMax) * yScale -
				    (float)((plotHeight-2)/2.0));
			}
		      }
		    }
		    point++;
		    nextLine = (plotLine *)nextLine->next;
		  }
		  if ((!showPhase) && (sPoints > 0) && (plotSource(jj)))
		    if (currentSource == -1)
		      XDrawPoints(myDisplay, activeDrawable, gcArray[chooseColor(jj)], data, sPoints,
				  CoordModeOrigin);
		    else
		      XDrawPoints(myDisplay, activeDrawable, gcArray[0], data, sPoints,
				  CoordModeOrigin);
		  else if (sPoints > 0) {
		    XDrawLines(myDisplay, activeDrawable, darkGreenGc, data, sPoints,
			       CoordModeOrigin);
		    jj = nSources;
		  }
		}
	      }
	      nBaselinesPlotted++;
	    }
	  }
	  free(data);
	}
	if (showPhase) {
	  int point;
	  
	  data = (XPoint *)malloc(nPScans*sizeof(XPoint));
	  if (data == NULL) {
	    perror("malloc of XPoints for phase");
	    exit(-1);
	  }
	  nBaselinesPlotted = 0;
	  for (iii = 0; (nBaselinesPlotted < totalBaselines) && (!redrawAbort); iii++) {
	    i = sortOrder[iii];
	    if (requestedBaselines[dataRoot->bsln[i].ant1][dataRoot->bsln[i].ant2] || showClosure) {
	      for (j = 0; j < nSidebands; j++) {
		int jj, sPoints;
		
		for (jj = 0; jj < nSources; jj++) {
		  sPoints = 0;
		  point = 0;
		  nextLine = dataRoot;
		  for (k = 0; k < nScans; k++) {
		    if (((currentSource == -1) || (currentSource == nextLine->sourceNumber)) &&
			plotSource(currentSource) &&
			(polarState(nextLine->bsln[i].ant1, nextLine->bsln[i].ant2, nextLine->polar) & polMask)) {
		      if ((nextLine->sourceNumber == jj) &&
			  (
			   ((((nextLine->bsln[i].flag > 0) && showGood) ||
			    ((nextLine->bsln[i].flag < 0) && showBad)) && (!showClosure)) || 
			   (showClosure &&
			    ( (showBad && showGood) ||
			      (showGood && (nextLine->bsln[closureMapping[i][0]].flag > 0) &&
			       (nextLine->bsln[closureMapping[i][1]].flag > 0) &&
			       (nextLine->bsln[closureMapping[i][2]].flag > 0)) ||
			      (showBad &&  (nextLine->bsln[closureMapping[i][0]].flag < 0) &&
			       (nextLine->bsln[closureMapping[i][1]].flag < 0) &&
			       (nextLine->bsln[closureMapping[i][2]].flag < 0)))))) {
			if ((point >= sScan) && (point <= eScan)) {
			  if ((trackFileVersion < 2) || (!timePlot))
			    data[sPoints].x = plotXSkip +
			      (int)(((float)(k-sScan)) * ((float)plotWidth) / ((float)nPScans));
			  else
			    data[sPoints].x = plotXSkip +
			      (int)((nextLine->uTC - uTCS) * ((float)plotWidth) /
				    (uTCE - uTCS));
			  if (showClosure) {
			    double closure;
 
			    if ((closureTriangle[i][0] < closureTriangle[i][1]) &&
				(closureTriangle[i][1] < closureTriangle[i][2]))
			      closure = nextLine->bsln[closureMapping[i][0]].phase[j+sBOffset] +
				nextLine->bsln[closureMapping[i][1]].phase[j+sBOffset] -
				nextLine->bsln[closureMapping[i][2]].phase[j+sBOffset];
			    else if ((closureTriangle[i][0] < closureTriangle[i][1]) &&
				     (closureTriangle[i][1] > closureTriangle[i][2]))
			      closure = nextLine->bsln[closureMapping[i][0]].phase[j+sBOffset] -
				nextLine->bsln[closureMapping[i][1]].phase[j+sBOffset] +
				nextLine->bsln[closureMapping[i][2]].phase[j+sBOffset];
			    else
			      closure = -nextLine->bsln[closureMapping[i][0]].phase[j+sBOffset] +
				nextLine->bsln[closureMapping[i][1]].phase[j+sBOffset] +
				nextLine->bsln[closureMapping[i][2]].phase[j+sBOffset];
			    while (closure >= 180.0)
			      closure -= 360.0;
			    while (closure <= -180.0)
			      closure += 360.0;
			    data[sPoints++].y = plotHeight/2 + charHeight + nBaselinesPlotted*bslnSkip +
			      j*plotHeight + (int)(closure * (float)(plotHeight-2) / 360.0);
			  } else
			    data[sPoints++].y = plotHeight/2 + charHeight + nBaselinesPlotted*bslnSkip +
			      j*plotHeight +
			      (int)(-nextLine->bsln[i].phase[j+sBOffset] * (float)(plotHeight-2) / 360.0);
			}
		      }
		    }
		    point++;
		    nextLine = (plotLine *)nextLine->next;
		  }
		  if ((!zoomed) && (nCellsPlotted > 1))
		    if (userSelectedPointSize > 0) {
		      int kk;
		      
		      if (currentSource == -1) {
			if (plotSource(jj))
			  for (kk = 0; kk < sPoints; kk++)
			    XFillArc(myDisplay, activeDrawable, gcArray[chooseColor(jj)],
				     data[kk].x-1, data[kk].y,
				     userSelectedPointSize, userSelectedPointSize, 0, 360*64);
		      } else
			for (kk = 0; kk < sPoints; kk++)
			  XFillArc(myDisplay, activeDrawable, whiteGc,
				   data[kk].x-1, data[kk].y,
				   userSelectedPointSize, userSelectedPointSize, 0, 360*64);
		      
		    } else if ((bslnSkip < 25) && (nPScans > 50) && (sPoints > 0)) {
		      if (currentSource == -1) {
			if (plotSource(jj))
			  XDrawPoints(myDisplay, activeDrawable, gcArray[chooseColor(jj)], data, sPoints,
				      CoordModeOrigin);
		      } else
			XDrawPoints(myDisplay, activeDrawable, gcArray[0], data, sPoints,
				    CoordModeOrigin);
		    } else if ((bslnSkip < 300)) {
		      int kk;
		      
		      if (currentSource == -1) {
			if (plotSource(jj))
			  for (kk = 0; kk < sPoints; kk++)
			    XFillArc(myDisplay, activeDrawable, gcArray[chooseColor(jj)],
				     data[kk].x-1, data[kk].y,
				     3+userSelectedPointSize-1, 3+userSelectedPointSize-1, 0, 360*64);
		      } else
			for (kk = 0; kk < sPoints; kk++)
			  XFillArc(myDisplay, activeDrawable, whiteGc,
				   data[kk].x-1, data[kk].y,
				   3+userSelectedPointSize-1, 3+userSelectedPointSize-1, 0, 360*64);
		    } else {
		      int kk;
		      
		      if (currentSource == -1) {
			if (plotSource(jj))
			  for (kk = 0; kk < sPoints; kk++)
			    XFillArc(myDisplay, activeDrawable, gcArray[chooseColor(jj)],
				     data[kk].x-2, data[kk].y,
				     5, 5, 0, 360*64);
		      } else
			for (kk = 0; kk < sPoints; kk++)
			  XFillArc(myDisplay, activeDrawable, whiteGc,
				   data[kk].x-2, data[kk].y,
				   5, 5, 0, 360*64);
		    } else {
		      int kk;
		      
		      if (currentSource == -1) {
			if (plotSource(jj))
			  for (kk = 0; kk < sPoints; kk++)
			    XFillArc(myDisplay, activeDrawable, gcArray[chooseColor(jj)],
				     data[kk].x-3, data[kk].y,
				     7+userSelectedPointSize-1, 7+userSelectedPointSize-1, 0, 360*64);
		      } else
			for (kk = 0; kk < sPoints; kk++)
			  XFillArc(myDisplay, activeDrawable, whiteGc,
				   data[kk].x-3, data[kk].y,
				   7+userSelectedPointSize-1, 7+userSelectedPointSize-1, 0, 360*64);
		    }
		}
	      }
	      nBaselinesPlotted++;
	    }
	  }
	  free(data);
	}
	/*
	  free malloced data structures
	*/
	ptr = dataRoot;
	while (ptr != NULL) {
	  lastPtr = (plotLine *)ptr->next;
	  free(ptr->bsln);
	  free(ptr);
	  ptr = lastPtr;
	}
	i = 0;
	while (i < nSources) {
	  free(sourceList[i++]);
	}
      }
    }
  }
  if ((!showRefresh) && (!redrawAbort));
    XCopyArea(myDisplay, pixmap, myWindow, whiteGc, 0, 0, displayWidth, displayHeight, 0, 0);
  drawnOnce = TRUE;
  unlock_X_display(TRUE);
  unlock_track("redrawScreenTrack()");
  inRedrawScreenTrack = FALSE;
}

void forceRedraw(char *caller)
{
    static XEvent Redraw_Event;
    static int first_call = TRUE;

    if (debugMessagesOn)
      printf("In forceRedraw (%s)\n", caller);
    if (first_call) {
      Redraw_Event.xexpose.type = Expose;
      Redraw_Event.xexpose.display = myDisplay;
      Redraw_Event.xexpose.window = myWindow;
      Redraw_Event.xexpose.send_event = TRUE;
      Redraw_Event.xexpose.x = Redraw_Event.xexpose.y =
	Redraw_Event.xexpose.width = Redraw_Event.xexpose.height = 1;
      Redraw_Event.xexpose.count = 0;
      first_call = FALSE;
    }
    lock_X_display();
    lock_malloc(NULL);
    if (debugMessagesOn)
      printf("Calling XSendEvent\n");
    XSendEvent(myDisplay, myWindow, FALSE, ExposureMask,
	       (XEvent *)&Redraw_Event);
    unlock_malloc(NULL);
    unlock_X_display(TRUE);
    internalEvent = TRUE;
    if (debugMessagesOn)
      printf("Exiting forceRedraw\n\n\n\n");
}

void printCorrelatorState(correlatorDef *ptr)
{
  int crate, bsln;
  static int lastScanNumber[N_CRATES];

  printf("In printCorrelatorCrateStatus\n");
  for (crate = 0; crate < N_CRATES; crate++) {
    printf("Checking crate %d\n", crate);
    if (ptr->header.crateActive[crate]) {
      printf("It's active %d, %d\n",
	     ptr->header.scanNumber[crate],
	     lastScanNumber[crate]);
      if (ptr->header.scanNumber[crate] !=
	  lastScanNumber[crate]) {
	printf("New data from crate %d, its scan # was %d, UTC = %f int = %f\n",
	       crate+1,
	       ptr->header.scanNumber[crate],
	       ptr->header.UTCTime[crate],
	       ptr->header.intTime[crate]);
	lastScanNumber[crate] = ptr->header.scanNumber[crate];
	printf("Chunk resolutions  1: %d 2: %d 3: %d 4: %d\n",
	       ptr->crate[crate].description.pointsPerChunk[0][0],
	       ptr->crate[crate].description.pointsPerChunk[0][1],
	       ptr->crate[crate].description.pointsPerChunk[0][2],
	       ptr->crate[crate].description.pointsPerChunk[0][3]);
	bsln = 0;
	while ((ptr->crate[crate].description.baselineInUse[activeRx][bsln]) &&
	       (bsln < N_BASELINES_PER_CRATE)) {
	  printf("Has baseline %d-%d\n",
		 ptr->crate[crate].data[bsln].antenna[0],
		 ptr->crate[crate].data[bsln].antenna[1]);
	  bsln++;
	}
      }
    }
  }
}

void setSensitivities(void)
{
    if (haveTrackDirectory)
      XtVaSetValues(scanToggle, XmNsensitive, True, NULL);
    else
      XtVaSetValues(scanToggle, XmNsensitive, False, NULL);
  if (scanMode) {
    /*
    XtVaSetValues(sourceButton, XmNsensitive, False, NULL);
    */
    XtVaSetValues(statsToggle, XmNsensitive, True, NULL);
    XtVaSetValues(plotToOneToggle, XmNsensitive, False, NULL);
    XtVaSetValues(cohToggle, XmNsensitive, False, NULL);
    XtVaSetValues(lagsToggle, XmNsensitive, True, NULL);
    XtVaSetValues(bandLabelingToggle, XmNsensitive, True, NULL);
    XtVaSetValues(integrateToggle, XmNsensitive, True, NULL);
    XtVaSetValues(showGoodToggle, XmNsensitive, False, NULL);
    XtVaSetValues(showBadToggle, XmNsensitive, False, NULL);
    XtVaSetValues(closureToggle, XmNsensitive, False, NULL);
    XtVaSetValues(timePlotToggle, XmNsensitive, False, NULL);
    XtVaSetValues(hAPlotToggle, XmNsensitive, False, NULL);
    XtVaSetValues(applySecantZToggle, XmNsensitive, False, NULL);
    XtVaSetValues(gridToggle, XmNsensitive, False, NULL);
    XtVaSetValues(scaleInMHzToggle, XmNsensitive, True, NULL);
    XtVaSetValues(blockButton, XmNsensitive, True, NULL);
    XtVaSetValues(chunkButton, XmNsensitive, True, NULL);
    XtVaSetValues(rangeButton, XmNsensitive, False, NULL);
    XtVaSetValues(timeRangeButton, XmNsensitive, False, NULL);
    XtVaSetValues(wrapColorToggle, XmNsensitive, False, NULL);
  } else {
    XtVaSetValues(lagsToggle, XmNsensitive, False, NULL);
    /*
    XtVaSetValues(sourceButton, XmNsensitive, True, NULL);
    */
    if (trackFileVersion >= 2)
      XtVaSetValues(timePlotToggle, XmNsensitive, True, NULL);
    else
      XtVaSetValues(timePlotToggle, XmNsensitive, False, NULL);
    if (trackFileVersion >= 3) {
      XtVaSetValues(hAPlotToggle, XmNsensitive, True, NULL);
      XtVaSetValues(applySecantZToggle, XmNsensitive, True, NULL);
    } else {
      XtVaSetValues(hAPlotToggle, XmNsensitive, False, NULL);
      XtVaSetValues(applySecantZToggle, XmNsensitive, False, NULL);
    }
    XtVaSetValues(rangeButton, XmNsensitive, True, NULL);
    XtVaSetValues(timeRangeButton, XmNsensitive, True, NULL);
    XtVaSetValues(gridToggle, XmNsensitive, True, NULL);
    XtVaSetValues(bandLabelingToggle, XmNsensitive, False, NULL);
    XtVaSetValues(plotToOneToggle, XmNsensitive, True, NULL);
    XtVaSetValues(cohToggle, XmNsensitive, True, NULL);
    XtVaSetValues(integrateToggle, XmNsensitive, False, NULL);
    XtVaSetValues(showGoodToggle, XmNsensitive, True, NULL);
    XtVaSetValues(showBadToggle, XmNsensitive, True, NULL);
    XtVaSetValues(closureToggle, XmNsensitive, True, NULL);
    XtVaSetValues(scaleInMHzToggle, XmNsensitive, False, NULL);
    XtVaSetValues(statsToggle, XmNsensitive, False, NULL);
    XtVaSetValues(blockButton, XmNsensitive, False, NULL);
    XtVaSetValues(chunkButton, XmNsensitive, False, NULL);
    XtVaSetValues(wrapColorToggle, XmNsensitive, True, NULL);
  }
}

void exitCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
    exit(0);
}

void resizeCB(Widget widget, XtPointer client_data, XtPointer call_data)

/*
  This routine is called when the window is resized.   The graphics portion of
  the window basically operates in two modes - big and small.   If the total
  display area is more than BIG_WINDOW pixels in area, then the display is
  switched to big mode, where the symbols and fonts are bigger.
*/

{
  /* XmDrawingAreaCallbackStruct* ptr; */
  Dimension width, height;
  int old_height, old_width;

  /* ptr = (XmDrawingAreaCallbackStruct*) call_data; */
  /*
  if (debugMessagesOn)
    printf("In resizeCB, reason = 0x%x, event = 0x%x\n", ptr->reason, (int)ptr->event);
  */
  if (blueGc != NULL) {
    XtVaGetValues(widget, XmNwidth, (Dimension *) &width,
		  XmNheight, (Dimension *) &height, NULL);
    old_width = displayWidth;
    old_height = displayHeight;
    displayWidth = (int)width;
    displayHeight = (int)height;
    if ((displayWidth*displayHeight) >= BIG_WINDOW) {
      XSetFont(myDisplay, blueGc, bigFontStruc->fid);
      XSetFont(myDisplay, whiteGc, bigFontStruc->fid);
      XSetFont(myDisplay, yellowGc, bigFontStruc->fid);
	XSetFont(myDisplay, redGc, bigFontStruc->fid);
	XSetFont(myDisplay, greenGc, bigFontStruc->fid);
	fieldSize = 2;
    }
    else {
      XSetFont(myDisplay, blueGc, smallFontStruc->fid);
      XSetFont(myDisplay, whiteGc, smallFontStruc->fid);
      XSetFont(myDisplay, yellowGc, smallFontStruc->fid);
      XSetFont(myDisplay, redGc, smallFontStruc->fid);
	XSetFont(myDisplay, greenGc, smallFontStruc->fid);
	fieldSize = 1;
    }
    if ((old_height != displayHeight) || (old_width != displayWidth)) {
      XFreePixmap(myDisplay, pixmap);
      pixmap = XCreatePixmap(myDisplay, myWindow, displayWidth, displayHeight, XDepth);
      if (debugMessagesOn)
	printf("resizeCB setting resizeEvent to TRUE\n");
      shouldSayRedrawing = TRUE;
      resizeEventSeen = shouldPlotResize = TRUE;
    }
  }
}

Widget CreatePulldownMenu(Widget parent, char* name, int tear_off)
{
    Widget	menu;
    Widget	cascade;
    Arg		args[20];
    Cardinal	n;

    n = 0;
    if (tear_off)
	XtSetArg(args[n], XmNtearOffModel, XmTEAR_OFF_ENABLED); n++;
    menu = XmCreatePulldownMenu(parent, name, args, n);
    n = 0;
    XtSetArg(args[n], XmNsubMenuId, menu); n++;
    cascade = XmCreateCascadeButton(parent, name, args, n);
    XtManageChild(cascade);
    return menu;
}

Widget CreateHelpMenu(Widget parent, char* name)
{
    Widget	menu;
    Widget	cascade;
    Arg		args[20];
    Cardinal	n;

    n = 0;
/*    XtSetArg(args[n], XmNtearOffModel, XmTEAR_OFF_ENABLED); n++; */
    menu = XmCreatePulldownMenu(parent, name, args, n);

    n = 0;
    XtSetArg(args[n], XmNsubMenuId, menu); n++;
    cascade = XmCreateCascadeButton(parent, name, args, n);
    XtManageChild(cascade);
    XtVaSetValues(parent, XmNmenuHelpWidget, cascade, NULL);
    return menu;
}

Widget CreatePushbutton(Widget parent, char* name,
    XtCallbackProc callback,
    XtPointer client_data)

{
    /* Create Push Button */
    Widget	push;
    Arg		args[20];
    Cardinal	n;

    n = 0;
    push = XmCreatePushButton(parent, name, args, n);

    /* Set up callback routine */
    XtAddCallback(push, XmNactivateCallback, callback, client_data);

    XtManageChild(push);

    return push;
}

void initGcs()
{

/* default pixel values */

  mybackground = BlackPixel(myDisplay, myscreen);
  myforeground = WhitePixel(myDisplay, myscreen);
/* default program-specified window position and size */

  cmap = DefaultColormap(myDisplay, DefaultScreen(myDisplay));
  red.red =65535; red.green = 10000; red.blue = 10000;
  if (XAllocColor(myDisplay, cmap, &red) == 0) {
    if (XAllocNamedColor(myDisplay, cmap, "red", &red, &red) == 0)
      printf("Red pixel value cannot be obtained from default colormap\r\n");
  }
  blue.red = 30000; blue.green = 50000; blue.blue = 65535;
  if (XAllocColor(myDisplay, cmap, &blue) == 0) {
    if (XAllocNamedColor(myDisplay, cmap, "blue", &blue, &blue) == 0)
      printf("Blue pixel value cannot be obtained from default colormap\r\n");
  }
  yellow.red = 65535; yellow.green = 55000; yellow.blue = 0;
  if (XAllocColor(myDisplay, cmap, &yellow) == 0) {
    if (XAllocNamedColor(myDisplay, cmap, "yellow", &yellow, &yellow) == 0)
      printf("Yellow pixel value cannot be obtained from default colormap\r\n");
  }
  if (XAllocNamedColor(myDisplay, cmap, "grey42", &grey, &grey) == 0)
    printf("Grey pixel value cannot be obtained from default colormap\r\n");
  if (XAllocNamedColor(myDisplay, cmap, "grey12", &darkGrey, &darkGrey) == 0)
    printf("dark grey pixel value cannot be obtained from default colormap\r\n");
  if (XAllocNamedColor(myDisplay, cmap, "sea green", &darkGreen, &darkGreen) == 0)
    printf("darkGreen pixel value cannot be obtained from default colormap\r\n");
  green.red =0; green.green = 65535; green.blue = 0;
  if (XAllocColor(myDisplay, cmap, &green) == 0) {
    if (XAllocNamedColor(myDisplay, cmap, "red", &red, &red) == 0)
      printf("Green pixel value cannot be obtained from default colormap\r\n");
  }
  white.red =65535; white.green = 65535; white.blue = 65535;
  if (XAllocColor(myDisplay, cmap, &white) == 0) {
    if (XAllocNamedColor(myDisplay, cmap, "white", &white, &white) == 0)
      printf("White pixel value cannot be obtained from default colormap\r\n");
  }
  black.red =0; black.green = 0; black.blue = 0;
  if (XAllocColor(myDisplay, cmap, &black) == 0) {
    if (XAllocNamedColor(myDisplay, cmap, "black", &black, &black) == 0)
      printf("Black pixel value cannot be obtained from default colormap\r\n");
  }
  
    /* GC creation and initialization */

    mygc = XCreateGC(myDisplay, myWindow, 0, 0);
    blueGc = XCreateGC(myDisplay, myWindow, 0, 0);
    whiteGc = XCreateGC(myDisplay, myWindow, 0, 0);
    blackGc = XCreateGC(myDisplay, myWindow, 0, 0);
    labelGc = XCreateGC(myDisplay, myWindow, 0, 0);
    yellowGc = XCreateGC(myDisplay, myWindow, 0, 0);
    greyGc = XCreateGC(myDisplay, myWindow, 0, 0);
    darkGreyGc = XCreateGC(myDisplay, myWindow, 0, 0);
    darkGreenGc = XCreateGC(myDisplay, myWindow, 0, 0);
    whiteBfGc = XCreateGC(myDisplay, myWindow, 0, 0);
    yellowBfGc = XCreateGC(myDisplay, myWindow, 0, 0);
    blueBfGc = XCreateGC(myDisplay, myWindow, 0, 0);
    redGc = XCreateGC(myDisplay, myWindow, 0, 0);
    greenGc = XCreateGC(myDisplay, myWindow, 0, 0);
    smallFontStruc = XLoadQueryFont(myDisplay, 
	"-adobe-new century schoolbook-medium-r-normal--11-*-*-*-*-*-*-*");
    if (smallFontStruc == 0) {
      smallFontStruc = XLoadQueryFont(myDisplay,
	"-misc-fixed-medium-r-semicondensed--13-100-100-100-c-60-iso8859-1");
    }
    if (smallFontStruc == 0) {
	printf("Small font could not be loaded - aborting\r\n");
	/*
	exit(0);
	*/
    }
    bigFontStruc = XLoadQueryFont(myDisplay, 
	"-adobe-new century schoolbook-medium-r-normal--14-*-*-*-*-*-*-*");
    if (bigFontStruc == 0) {
      bigFontStruc = XLoadQueryFont(myDisplay,
	"-misc-fixed-medium-r-semicondensed--13-100-100-100-c-60-iso8859-1");
    }
    if (bigFontStruc == 0) {
	printf("Big font could not be loaded - aborting\r\n");
	/*
	exit(0);
	*/
    }
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, blueGc);
    XSetFont(myDisplay, blueGc, smallFontStruc->fid);
    XSetBackground(myDisplay, mygc, mybackground);
    XSetForeground(myDisplay, mygc, myforeground);
    XSetBackground(myDisplay, blueGc, mybackground);
    XSetForeground(myDisplay, blueGc, blue.pixel);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, whiteGc);
    XSetFont(myDisplay, whiteGc, smallFontStruc->fid);
    XSetBackground(myDisplay, whiteGc, mybackground);
    XSetForeground(myDisplay, whiteGc, white.pixel);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, blackGc);
    XSetFont(myDisplay, blackGc, smallFontStruc->fid);
    XSetBackground(myDisplay, blackGc, mybackground);
    XSetForeground(myDisplay, blackGc, mybackground);
    XCopyGC(myDisplay, whiteGc, 0x7fffff, labelGc);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, whiteBfGc);
    XSetFont(myDisplay, whiteBfGc, smallFontStruc->fid);
    XSetBackground(myDisplay, whiteBfGc, mybackground);
    XSetForeground(myDisplay, whiteBfGc, white.pixel);
    XSetLineAttributes(myDisplay, whiteBfGc, 2, LineSolid, CapRound,
	JoinMiter);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, yellowGc);
    XSetFont(myDisplay, yellowGc, smallFontStruc->fid);
    XSetBackground(myDisplay, yellowGc, mybackground);
    XSetForeground(myDisplay, yellowGc, yellow.pixel);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, greyGc);
    XSetFont(myDisplay, greyGc, smallFontStruc->fid);
    XSetBackground(myDisplay, greyGc, mybackground);
    XSetForeground(myDisplay, greyGc, grey.pixel);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, darkGreyGc);
    XSetFont(myDisplay, darkGreyGc, smallFontStruc->fid);
    XSetBackground(myDisplay, darkGreyGc, mybackground);
    XSetForeground(myDisplay, darkGreyGc, darkGrey.pixel);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, darkGreenGc);
    XSetFont(myDisplay, darkGreenGc, smallFontStruc->fid);
    XSetBackground(myDisplay, darkGreenGc, mybackground);
    XSetForeground(myDisplay, darkGreenGc, darkGreen.pixel);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, yellowBfGc);
    XSetFont(myDisplay, yellowBfGc, smallFontStruc->fid);
    XSetBackground(myDisplay, yellowBfGc, mybackground);
    XSetForeground(myDisplay, yellowBfGc, yellow.pixel);
    XSetLineAttributes(myDisplay, yellowBfGc, 2, LineSolid, CapRound,
	JoinMiter);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, blueBfGc);
    XSetFont(myDisplay, blueBfGc, smallFontStruc->fid);
    XSetBackground(myDisplay, blueBfGc, mybackground);
    XSetForeground(myDisplay, blueBfGc, blue.pixel);
    XSetLineAttributes(myDisplay, blueBfGc, 2, LineSolid, CapRound,
	JoinMiter);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, redGc);
    XSetFont(myDisplay, redGc, smallFontStruc->fid);
    XSetBackground(myDisplay, redGc, mybackground);
    XSetForeground(myDisplay, redGc, red.pixel);
    XCopyGC(myDisplay, mygc, GCForeground | GCBackground, greenGc);
    XSetFont(myDisplay, greenGc, smallFontStruc->fid);
    XSetBackground(myDisplay, greenGc, mybackground);
    XSetForeground(myDisplay, greenGc, green.pixel);
}

void showHelp(char *key, char *function, int line, int col)
{
  int vpos;

  vpos = 25+(line-1)*12;
  if (col == 0)
    XDrawImageString(myDisplay, myWindow, whiteGc, 10, vpos, key, strlen(key));
  XDrawImageString(myDisplay, myWindow, whiteGc, 60+(col*320), vpos, function, strlen(function));
}

void printHelp()
{
  int nChars, width;
  char textLine[200];

  XClearWindow(myDisplay, myWindow);
  sprintf(textLine, "K E Y B O A R D      S H O R T C U T S                                                            M O U S E     F U N C T I O N S");
  nChars = strlen(textLine);
  XDrawImageString(myDisplay, myWindow, whiteGc, 10, 10, textLine, nChars);
  showHelp("0",     "Display data for all baselines",                               1, 0);
  showHelp("1...9", "Display data for baseline n-*, or closure triangles with n",   2, 0);
  showHelp("a",     "Toggle amplitude on/off",                                      3, 0);
  showHelp("A",     "Toggle amplitude autoscale on/off",                            4, 0);
  showHelp("b",     "Blank/Unblank the display (useful for resizing)",              5, 0);
  showHelp("c",     "Toggle closure phase on/off (mir-mode only)",                  6, 0);
  showHelp("d",     "Display both sidebands",                                       7, 0);
  showHelp("D",     "Toggle debugging messages on/off",                             8, 0);
  showHelp("e",     "Erase all spectral line markers",                              9, 0);
  showHelp("f",     "Toggle freeze mode (disable/enable updates)",                 10, 0);
  showHelp("g",     "Toggle autoscale (scan-mode only)",                           11, 0);
  showHelp("h",     "Display this screen",                                         12, 0);
  showHelp("i",     "Toggle integrate mode (scan-mode only)",                      13, 0);
  showHelp("k",     "Display coherence instead of ampiltude (toggle)",             14, 0);
  showHelp("l",     "Display lower sideband",                                      15, 0);
  showHelp("m",     "Display current track in mir-mode",                           16, 0);
  showHelp("o",     "Show all objects (mir-mode only)",                            17, 0);
  showHelp("p",     "Toggle phase on/off",                                         18, 0);
  showHelp("q",     "Quit this program",                                           19, 0);
  if (doubleBandwidth)
    showHelp("r",   "Toggle high/low 2 GHz bandwidth",                             20, 0);
  else
    showHelp("r",   "Toggle receiver high/low",                                    20, 0);
  showHelp("s",     "Display in scan mode",                                        21, 0);
  showHelp("S",     "Toggle display of SWARM correlator data",                     22, 0);
  showHelp("t",     "Enable/Disable the display of refreshes",                     23, 0);
  showHelp("u",     "Display upper sideband",                                      24, 0);
  showHelp("z",     "Toggle amp/coh plot form zero",                               25, 0);
  showHelp("*",     "Reset all filters (plot everything)",                         26, 0);
  showHelp("!",     "Turn on debugging messages",                                  27, 0);
  showHelp("%",     "Toggle sampler statistics (scan-mode only)",                  28, 0);
  showHelp("<tab>", "Toggle between mir-mode and scan-mode",                       29, 0);
  showHelp(".",     "Revert to default phase point size",                          30, 0);
  showHelp("-",     "Decrement point size by 1 pixel",                             31, 0);
  showHelp("+",     "Increment point size by 1 pixel",                             32, 0);
  showHelp("^",     "Toggle between sorted by baseline and normal sort",           33, 0);
  showHelp("(",     "Rotate colors left (mir mode only)",                          34, 0);
  showHelp(")",     "Rotate colors right (mir mode only)",                         35, 0);
  showHelp("=",     "Plot full track (mir mode only)",                             36, 0);
  showHelp("<ESC>n","Don't plot baselines with antenna n",                         37, 0);
  showHelp("@",     "SWARM Autocorrelations",                                      38, 0);
  showHelp(" ",     "Left click on source name to select only that source",         1, 1);
  showHelp(" ",     "Right click on source name to remove from display",            2, 1);
  showHelp(" ",     "Click within plot to zoom",                                    3, 1);
  showHelp(" ",     "Left click within plot to unzoom",                             4, 1);
  showHelp(" ",     "Right click within zoomed plot to label spectral line",        5, 1);
  showHelp(" ",     "Click on baseline or sideband label to filter.",               6, 1);
  showHelp(" ",     "Left mouse button selects time or scan range (mir-mode)",  7, 1);
  sprintf(textLine, "Hit any key to exit this screen (it won't be interpreted)");
  nChars = strlen(textLine);
  width = stringWidth(textLine);
  XDrawImageString(myDisplay, myWindow, whiteGc, (displayWidth-width)/2, 432, textLine, nChars);
  XFlush(myDisplay);
}

void drawCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int drawCBCount = 0;
  XmDrawingAreaCallbackStruct* ptr;
  XEvent eventReturn;

  if (debugMessagesOn)
    printf("In drawCB rES = %d, dCBC = %d, sR = %d, iE = %d\n",
	   resizeEventSeen, drawCBCount, showRefresh, internalEvent);
  if ((!resizeEventSeen) && (drawCBCount > 0) && (!showRefresh) && (!internalEvent))
    XCopyArea(myDisplay, pixmap, myWindow, whiteGc, 0, 0, displayWidth, displayHeight, 0, 0);
  internalEvent = FALSE;
  while (XCheckWindowEvent(myDisplay, myWindow, ExposureMask, &eventReturn))
    if (debugMessagesOn)
      printf("Soaking up event of type %d\n", eventReturn.type);
  ptr = (XmDrawingAreaCallbackStruct*) call_data;
  if ((!resizeEventSeen) || (drawCBCount < 2)) {
    if ((!drawCBCalledByTimer) &&
	(!((ptr->event->xexpose.width == 1) && (ptr->event->xexpose.height == 1)))) {
      resizeEventSeen = TRUE;
      
      shouldPlotResize = TRUE;
      
    }
    if ((drawCBCount++ > 1000) && debugMessagesOn)
      printf("In drawCB with drawCBCount = %d\n", drawCBCount);
    if (debugMessagesOn)
      printf("In drawCB, resizing = %d\n", resizing);
    if (debugMessagesOn) {
      printf("In drawCB, ptr->event->xexpose.count = %d, resizing = %d\n",
	     ptr->event->xexpose.count, resizing);
      printf("x = %d, y = %d, width = %d, height = %d\n",
	     ptr->event->xexpose.x,
	     ptr->event->xexpose.y,
	     ptr->event->xexpose.width,
	     ptr->event->xexpose.height
	     );
    }
    if (ptr->event->xexpose.count == 0) {
      if ((drawCBCount < 3) || drawCBCalledByTimer || (!havePlottedSomething) || newPoints ||
	  (!((ptr->event->xexpose.width == 1) &&
	     (ptr->event->xexpose.height == 1)))) {
	newPoints = FALSE;
	if (debugMessagesOn) {
	  printf("Gonna redraw...\n");
	  printf("shouldPlotResize = %d, scanMode = %d, inRedrawScreenTrack = %d\n",
		 shouldPlotResize, scanMode, inRedrawScreenTrack);
	}
	if (!shouldPlotResize || TRUE) {
	  resizeEventSeen = shouldPlotResize = FALSE;
	  if (helpScreenActive)
	    printHelp();
	  else if (scanMode) {
	    redrawScreen();
	  } else {
	    while (inRedrawScreenTrack) {
	      if (debugMessagesOn)
		printf("Looping until track update completes\n");
	      usleep(100);
	    }
	    redrawScreenTrack();
	  }
	}
	resizing = FALSE;
      } else
	if (debugMessagesOn)
	  printf("NOT gonna redraw\n");
    }
  }
  drawCBCalledByTimer = FALSE;
}

void *timer(void *arg)
{
  while (TRUE) {
    if (shouldPlotResize) {
      while (resizeEventSeen && shouldPlotResize) {
	while (resizeEventSeen) {
	  if (debugMessagesOn)
	    printf("Looping in timer\n");
	  resizeEventSeen = FALSE;
	  usleep(1000000);
	}
	drawCBCalledByTimer = TRUE;
	forceRedraw("timer");
	shouldPlotResize = resizeEventSeen = FALSE;
      }
    }
    usleep(100000);
  }
}

void checkForDoubleBandwidth(void)
{
  FILE *dummy;

  dummy = fopen("/global/configFiles/doubleBandwidth", "r");
  if (dummy == NULL)
    doubleBandwidth = FALSE;
  else {
    doubleBandwidth = TRUE;
    fclose(dummy);
  }
}

/*
  sleeper runs as a thread - it looks for changes in the shared memory
  structure written by corrSaver. If a change is seen, a local
  copy of the data is made, and a screen refresh is queued
*/
void *sleeper(void *arg)
{
  int returnCode;
  correlatorDef *cptr;
  int changed;
  static int lastScanNumber[N_CRATES];
    
  dprintf("Open shared memory segment with key = %d\n", PLT_KEY_ID);
  returnCode = shmget(PLT_KEY_ID,
		      sizeof(correlatorDef),
		      0444);
  if (returnCode < 0) {
    /*
    perror("creating main shared memory structure");
    exit(-1);
    */
    fprintf(stderr, "corrSaver not running on this machine - only mir-mode can be used.\n");
    corrSaverMachine = FALSE;
    scanMode = FALSE;
  }
  dprintf("Create successful\n");
  cptr = shmat(returnCode, (char *)0, SHM_RDONLY);
  if (cptr < 0)
    if (debugMessagesOn)
      perror("shmat call");
  while (!drawnOnce)
    usleep(10000);
  while (TRUE) {
    int crate;
    struct stat messageStat, oldMessageStat;

    checkForDoubleBandwidth();
    if (scanMode && corrSaverMachine) {
      oldMessageStat.st_mtime = 0;
      changed = TRUE;
      if (!(cptr->updating)) {
	for (crate = 0; crate < N_CRATES; crate++)
	  if (cptr->header.crateActive[crate]) {
	    if ((cptr->header.scanNumber[crate] == lastScanNumber[crate]) &&
		((cptr->header.scanNumber[crate] > 0))) {
	      changed = FALSE;
	    }
	  }
	if (debugMessagesOn && 0)
	  printCorrelatorState(cptr);
	if (changed) {
	  newPoints = TRUE;
	  lock_data();
	  for (crate = 0; crate < N_CRATES; crate++)
	    if (cptr->header.crateActive[crate])
	      lastScanNumber[crate] = cptr->header.scanNumber[crate];
	  if (!integrate) {
	    /* Copy the entire correlator data structure from shared memory */
	    bcopy(cptr, &correlator, sizeof(correlatorDef));
	    nIntegrations = 1;
	  } else {
	    int bsln, sb, rx, channel;
	    char currentSource[100];
	    FILE *projectInfo;

	    bcopy(cptr, &scratchCorrelatorCopy, sizeof(correlatorDef));
	    projectInfo = fopen("/sma/rtdata/engineering/monitorLogs/littleLog.txt", "r");
	    if (projectInfo != NULL) {
	      fscanf(projectInfo, "%s", &currentSource[0]);
	      fclose(projectInfo);
	    } else
	      currentSource[0] = (char)0;
	    if (!strcmp(currentSource, integrateSource)) {
	      /* Integrate the data */
	      for (crate = 0; crate < N_CRATES; crate++)
		if (scratchCorrelatorCopy.header.crateActive[crate])
		  for (bsln = 0; bsln < N_BASELINES_PER_CRATE; bsln++) {
		    /* Copy the sampler statistics for the antennas on this baseline */
		    bcopy(scratchCorrelatorCopy.crate[crate].data[bsln].counts,
			  correlator.crate[crate].data[bsln].counts,
			  N_IFS*N_ANTENNAS_PER_BASELINE*N_CHUNKS*N_SAMPLER_LEVELS*sizeof(float));
		    for (rx = 0; rx < N_IFS; rx++)
		      if (scratchCorrelatorCopy.crate[crate].description.baselineInUse[rx][bsln])
			for (sb = 0; sb < N_SIDEBANDS; sb++)
			  for (channel = 0; channel < N_CHANNELS_MAX; channel++) {
			    float realI, imagI, real, imag;
			    
			    realI = scratchCorrelatorCopy.crate[crate].data[bsln].amp[rx][sb][channel]*
			      cos(scratchCorrelatorCopy.crate[crate].data[bsln].phase[rx][sb][channel]);
			    imagI = scratchCorrelatorCopy.crate[crate].data[bsln].amp[rx][sb][channel]*
			      sin(scratchCorrelatorCopy.crate[crate].data[bsln].phase[rx][sb][channel]);
			    real = correlator.crate[crate].data[bsln].amp[rx][sb][channel]*
			      cos(correlator.crate[crate].data[bsln].phase[rx][sb][channel]);
			    imag = correlator.crate[crate].data[bsln].amp[rx][sb][channel]*
			      sin(correlator.crate[crate].data[bsln].phase[rx][sb][channel]);
			    real *= (float)(nIntegrations-1);
			    real += realI;
			    real /= (float)nIntegrations;
			    imag *= (float)(nIntegrations-1);
			    imag += imagI;
			    imag /= (float)nIntegrations;
			    correlator.crate[crate].data[bsln].amp[rx][sb][channel] =
			      sqrt(real*real + imag*imag);
			    correlator.crate[crate].data[bsln].phase[rx][sb][channel] =
			      atan2(imag, real);
			  }
		  }
	      nIntegrations++;
	    }
	  }
	  unlock_data();
	}
      }  else if (debugMessagesOn) {
	printf("Update blocked by writer\n");
      }
    } else {
      char fileName[100];
      FILE *dummy;

      changed = TRUE;
      if (trackFileVersion == 4)
	sprintf(fileName, "%s/plot_me_5_rx0", trackDirectory);
      if (trackFileVersion == 3)
	sprintf(fileName, "%s/plot_me_4_rx0", trackDirectory);
      else if (trackFileVersion == 2)
	sprintf(fileName, "%s/plot_me_3_rx0", trackDirectory);
      else
	sprintf(fileName, "%s/plot_me", trackDirectory);
      dummy = fopen(fileName, "r");
      if (dummy != NULL) {
	stat(fileName, &messageStat);
	if (messageStat.st_mtime == oldMessageStat.st_mtime) {
	  changed = FALSE;
	} else {
	  oldMessageStat.st_mtime = messageStat.st_mtime;
	  newPoints = TRUE;
	}
	fclose(dummy);
      }
    }
    if (changed && (!disableUpdates))
      forceRedraw("sleeper");
    if (scanMode)
      sleep(5+interscanPause);
    else
      sleep(5+interscanPause);
  }
}

void bfCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
    XtManageChild(chunkDialog);
}

void say_bad_float()
{
    static int		first_call = True;
    static Widget	bad_float_dialog;
    Widget		cancel_button, help_button;
    Cardinal		n;
    Arg			args[20];

    if (first_call) {
	n = 0;
	bad_float_dialog = XmCreateErrorDialog(rootParent, "bad_float",
	    args, n);
	cancel_button = XmMessageBoxGetChild(bad_float_dialog, XmDIALOG_CANCEL_BUTTON);
	XtUnmanageChild(cancel_button);
	help_button = XmMessageBoxGetChild(bad_float_dialog, XmDIALOG_HELP_BUTTON);
	XtUnmanageChild(help_button);
	XtAddCallback(bad_float_dialog, XmNokCallback, (XtCallbackProc) bfCB,
	    (XtPointer) "bad_float");
	first_call = False;
    }
    XtManageChild(bad_float_dialog);
}

int is_floating(char* string)
{
    int	i, found_bad;

    found_bad = False;
    i = 0;
    while ((!found_bad) && (i < strlen(string)) && (i < 1000)) {
	if ((!isdigit(string[i])) && (string[i] != '.') && (string[i] != '-'))
	    found_bad = True;
	i++;
    }
    if ((i == 0) || (i == 1000))
	found_bad = True;
    return (!found_bad);
}

void fileOpenCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  strcpy(trackDirectory, string);
  if (debugMessagesOn)
    printf("The track directory is now \"%s\"\n", trackDirectory);
  scanMode = FALSE;
  havePlottedSomething = FALSE;
  haveTrackDirectory = TRUE;
  setSensitivities();
  XmToggleButtonSetState(scanToggle, FALSE, FALSE);
  XtUnmanageChild(trackFileDialog);
  startScan = endScan = -1;
  forceRedraw("fileOpenCB");
  XtFree(string);
}

void bslnOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;
  char *token;
  int i, j, defaultValue, selectValue;
  int badToken = FALSE;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  strcpy(bslnFilter, string);
  if (negateBsln) {
    defaultValue = 1;
    selectValue = 0;
  } else {
    defaultValue = 0;
    selectValue = 1;
  }
  for (i = 0; i < 11; i++)
    for (j = 0; j < 11; j++)
      requestedBaselines[i][j] = defaultValue;
  token = NULL;
  do {
    if (!token)
      token = strtok(string, " ");
    else
      token = strtok(NULL, " ");
    if (token) {
      int ant1, ant2, tokenOffset;

      if (token[0] == '*')
	ant1 = -1;
      /*
	else if ((token[0] < '1') || (token[0] > '9'))
	badToken = TRUE;
      */
      else
	/*
	  ant1 = (int)(token[0]-'0');
	*/
	sscanf(&token[0], "%d-%d", &ant1, &ant2);
      if (ant1 == 10)
	tokenOffset = 3;
      else
	tokenOffset = 2;
      if (token[tokenOffset] == '*')
	ant2 = -1;
      /*
      else if ((token[tokenOffset] < '1') || (token[tokenOffset] > '9'))
	badToken = TRUE;
      */
      else
	/*
	  ant2 = (int)(token[tokenOffset]-'0');
	*/
	sscanf(&token[tokenOffset], "%d", &ant2);
      if ((ant1 < -1) || (ant1 == 0) || (ant1 > 10) || (ant2 < -1) || (ant2 == 0) || (ant2 > 10))
	badToken = TRUE;
      if (!badToken) {
	for (i = 0; i < 11; i++)
	  for (j = 0; j < 11; j++)
	    if (((ant1 == -1) || (ant1 == i)) &&
		((ant2 == -1) || (ant2 == j)))
	      requestedBaselines[i][j] =
		requestedBaselines[j][i] = selectValue;
      }
    }
  } while ((token != NULL) && (!badToken)) ;
  if (badToken) {
    sprintf(bslnFilter, "*");
    for (i = 0; i < 11; i++)
      for (j = 0; j < 11; j++)
	requestedBaselines[i][j] = selectValue;
  }
  if (debugMessagesOn)
    for (i = 0; i < 11; i++) {
      for (j = 0; j < 11; j++)
	printf("%d ", requestedBaselines[i][j]);
      printf("\n");
  }
  forceRedraw("bslnOKCallback");
  XtFree(string);
}

void sourceOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;
  
  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  strcpy(sourceFilter, string);
  forceRedraw("sourceOKCB");
  XtFree(string);
}

void blockOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;
  int newBlocks[N_BLOCKS], i;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  strcpy(blockFilter, string);
  if (!strcmp(blockFilter, "*")) {
    nBlocksRequested = N_BLOCKS;
    for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
      requestedBlockList[i] = 1;
  } else {
    int newBlockCount;

    newBlockCount = sscanf(blockFilter, "%d %d %d %d %d %d",
			   &newBlocks[0],
			   &newBlocks[1],
			   &newBlocks[2],
			   &newBlocks[3],
			   &newBlocks[4],
			   &newBlocks[5]);
    if (newBlockCount > 0) {
      nBlocksRequested = newBlockCount;
      for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
	requestedBlockList[i] = 0;
      for (i = 0; i < nBlocksRequested; i++)
	if ((newBlocks[i] > 0) && (newBlocks[i] <= N_BLOCKS))
	  requestedBlockList[newBlocks[i]-1] = 1;
    } else {
      sprintf(blockFilter, "*");
      nBlocksRequested = N_BLOCKS;
      for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
	requestedBlockList[i] = 1;
    }
  }
  forceRedraw("blockOKCB");
  XtFree(string);
}

void rangeOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;
  int newStart, newEnd, i;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  if (!strcmp(string, "*"))
    startScan = endScan = -1;
  else {
    i = sscanf(string, "%d %d", &newStart, &newEnd);
    if ((i == 2) && (newStart >= 0) && (newEnd >= 0) &&
	(newStart < newEnd)) {
      startScan = newStart;
      endScan = newEnd;
    }
  }
  forceRedraw("rangeOKCB");
  XtFree(string);    
}

void bTimeCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XtManageChild(timeRangeDialog);
}

void say_bad_times()
{
  static int          first_call = True;
  static Widget       bad_times_dialog;
  Widget              cancel_button, help_button;
  Cardinal            n;
  XmString            info_Xstring;
  Arg                 args[20];
                                                                                                                
  if (first_call) {
    n = 0;
    bad_times_dialog = XmCreateErrorDialog(rootParent, "bad_times",
					      args, n);
    cancel_button = XmMessageBoxGetChild(bad_times_dialog, XmDIALOG_CANCEL_BUTTON);
    XtUnmanageChild(cancel_button);
    help_button = XmMessageBoxGetChild(bad_times_dialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(bad_times_dialog, XmNokCallback, (XtCallbackProc) bTimeCB,
		  (XtPointer) "bad_times");
    info_Xstring = XmStringCreateSimple("Please enter two times in HH:MM:SS format, separated by a space");
    XtVaSetValues(bad_times_dialog, XmNmessageString, info_Xstring, NULL);
    XmStringFree(info_Xstring);
    first_call = False;
  }
  XtManageChild(bad_times_dialog);
}

void timeRangeOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;
  int i;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  if (!strcmp(string, "*"))
    startTime = endTime = -100.0;
  else {
    int sHH, sMM, sSS, eHH, eMM, eSS;

    i = sscanf(string, "%d:%d:%d %d:%d:%d", &sHH, &sMM, &sSS, &eHH, &eMM, &eSS);
    if ((i == 6) &&
	(sHH >= 0) && (sHH < 24) &&
	(sMM >= 0) && (sMM < 60) &&
	(sSS >= 0) && (sSS < 60) &&
	(eHH >= 0) && (eHH < 24) &&
	(eMM >= 0) && (eMM < 60) &&
	(eSS >= 0) && (eSS < 60)) {
      startTime = (double)sHH + (double)sMM/60.0 + (double)sSS/3600.0;
      endTime   = (double)eHH + (double)eMM/60.0 + (double)eSS/3600.0;
    } else
      say_bad_times();
  }
  forceRedraw("timeRangeOKCB");
  XtFree(string);    
}

void testOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;
  int newStart, newEnd, i;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  if (!strcmp(string, "*"))
    startScan = endScan = -1;
  else {
    i = sscanf(string, "%d %d", &newStart, &newEnd);
    if ((i == 2) && (newStart >= 0) && (newEnd >= 0) &&
	(newStart < newEnd)) {
      startScan = newStart;
      endScan = newEnd;
    }
  }
  forceRedraw("testOKCB");
  XtFree(string);    
}

void chunkOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;
  int newChunks[4], i;
  
  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  strcpy(chunkFilter, string);
  if (!strcmp(chunkFilter, "*")) {
    nChunks = 4;
    for (i = 0; i < N_CHUNKS; i++)
      chunkList[i] = i;
  } else {
    int newChunkCount;

    newChunkCount = sscanf(chunkFilter, "%d %d %d %d",
			   &newChunks[0],
			   &newChunks[1],
			   &newChunks[2],
			   &newChunks[3]);
    if (newChunkCount > 0) {
      nChunks = newChunkCount;
      for (i = 0; i < nChunks; i++)
	chunkList[i] = newChunks[i] - 1;
    } else {
      sprintf(chunkFilter, "*");
      nChunks = 4;
      for (i = 0; i < N_CHUNKS; i++)
	chunkList[i] = i;
    }
  }
  forceRedraw("chunkOKCB");
  XtFree(string);
}

void sBOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  if ((string[0] == 'l') || (string[0] == 'L')) {
    nSidebands = 1;
    sBList[0] = 0;
    sBFilter[0] = 'L';
  } else if ((string[0] == 'u') || (string[0] == 'U')) {
    nSidebands = 1;
    sBList[0] = 1;
    sBFilter[0] = 'U';
  } else {
    nSidebands = 2;
    sBList[0] = 0;
    sBList[1] = 1;
    sBFilter[0] = '*';
  }
  forceRedraw("sBOKCB");
  XtFree(string);
}

void chanOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  int nRead, max, min;
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  nRead = sscanf(string, "%d %d", &min, &max);
  if ((nRead < 2) || (max <= 0) || (min < 0))
    max = min = 0;
  if (min > max) {
    int temp;

    temp = max;
    max = min;
    min = temp;
  }
  sWARMZoomedMax = max;
  sWARMZoomedMin = min;
  if ((string[0] == 'l') || (string[0] == 'L')) {
    nSidebands = 1;
    sBList[0] = 0;
    sBFilter[0] = 'L';
  } else if ((string[0] == 'u') || (string[0] == 'U')) {
    nSidebands = 1;
    sBList[0] = 1;
    sBFilter[0] = 'U';
  } else {
    nSidebands = 2;
    sBList[0] = 0;
    sBList[1] = 1;
    sBFilter[0] = '*';
  }
  forceRedraw("chanOKCB");
  XtFree(string);
}

void rxOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  if ((string[0] == 'l') || (string[0] == 'L')) {
    activeRx = LOW_RX_CODE;
    if (doubleBandwidth)
      sprintf(rxFilter, "Lower 2 GHz ");
    else
      sprintf(rxFilter, "Low Freq. Rx");
  } else if ((string[0] == 'h') || (string[0] == 'H')) {
    activeRx = HIGH_RX_CODE;
    if (doubleBandwidth)
      sprintf(rxFilter, "Upper 2 GHz  ");
    else
      sprintf(rxFilter, "High Freq. Rx");
  }
  forceRedraw("sBOKCB");
  XtFree(string);
}

void pointOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  sscanf(string, "%d", &userSelectedPointSize);
  if (userSelectedPointSize < 2)
    userSelectedPointSize = 2;
  forceRedraw("pointOKCB");
  XtFree(string);
}

void sWARMAveOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  sscanf(string, "%d", &userSelectedSWARMAverage);
  forceRedraw("sWARMAveOKCB");
  XtFree(string);
}

void closureBaseAntOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  sscanf(string, "%d", &closureBaseAnt);
  if (closureBaseAnt < 1)
    closureBaseAnt = 1;
  if (closureBaseAnt > 10)
    closureBaseAnt = 10;
  forceRedraw("closureBaseAntOKCB");
  XtFree(string);
}

void pauseOKCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  XmSelectionBoxCallbackStruct* ptr;
  char* string;

  ptr = (XmSelectionBoxCallbackStruct *) call_data;
  XmStringGetLtoR(ptr->value, XmSTRING_DEFAULT_CHARSET, &string);
  sscanf(string, "%d", &interscanPause);
  forceRedraw("pauseOKCB");
  XtFree(string);
}

void currentTrackCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  if (debugMessagesOn)
    printf("I'm in currentTrackCB\n");
  /*
    Set most recent directory to be the default
  */
  selectCurrentTrack();
  scanMode = FALSE;
  havePlottedSomething = FALSE;
  setSensitivities();
  XmToggleButtonSetState(scanToggle, FALSE, FALSE);
  startScan = endScan = -1;
  forceRedraw("currentTrackCB");
}

void trackCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int first_call = True;
  static char *defaultDirectory = "/sma/rtdata/science/mir_data/";
  char fullFileName[1000];
  char lastFile[1000];
  int latest = 0;
  static XmString dir, defDir;
  struct dirent *nextEnt;
  DIR *dirPtr;
  struct stat statBuffer;
  static char *mirDir = "/sma/rtdata/science/mir_data/";
  Cardinal		n;
  Arg			args[20];

  if (debugMessagesOn)
    printf("I'm in trackCB\n");
  if (first_call) {
    n = 0;
    trackFileDialog = XmCreateFileSelectionDialog(rootParent,
						  "Open Track file",
						  args, n);
    XtAddCallback(trackFileDialog, XmNcancelCallback, 
		  (void *)XtUnmanageChild, NULL);
    XtAddCallback(trackFileDialog, XmNokCallback, fileOpenCB, NULL);
    XtUnmanageChild(XmSelectionBoxGetChild(trackFileDialog,
					   XmDIALOG_HELP_BUTTON));
    dir=XmStringCreateLocalized(defaultDirectory);
    first_call = False;
  }
  /*
    Set most recent directory to be the default
  */
  dirPtr = opendir(mirDir);
  while ((nextEnt = readdir(dirPtr)) != NULL) {
    if (strstr(nextEnt->d_name, ".") == NULL) {
      sprintf(fullFileName, "%s%s", mirDir, nextEnt->d_name);
      stat(fullFileName, &statBuffer);
      if (debugMessagesOn)
	printf("Dir looping (%s) (%d, %d)!\n", fullFileName,
	       latest, (int)statBuffer.st_mtime);
      if (statBuffer.st_mtime > latest) {
	if (debugMessagesOn)
	  printf("This one's the youngest so far!\n");
	strcpy(lastFile, fullFileName);
	latest = statBuffer.st_mtime;
      }
    }
  }
  if (debugMessagesOn)
    printf("The most recent file is %s\n",
	   lastFile);
  defDir = XmStringCreateLocalized(lastFile);
  XtVaSetValues(trackFileDialog,
		XmNdirectory, dir, NULL);
  XtVaSetValues(trackFileDialog,
		XmNdirSpec, defDir, NULL);
  XtVaSetValues(trackFileDialog,
		XmNtextString, defDir, NULL);
  XmStringFree(defDir);
  XtManageChild(trackFileDialog);
}

void sourceCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];
  
  if (first_call) {
    n = 0;
    sourceDialog = XmCreatePromptDialog(rootParent, "source", args, n);
    help_button = XmSelectionBoxGetChild(sourceDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(sourceDialog, XmNokCallback, (XtCallbackProc) sourceOKCB,
		  (XtPointer) "source");
    first_call = False;
  }
  XtManageChild(bslnDialog);
}

void blockCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];
  
  if (first_call) {
    n = 0;
    blockDialog = XmCreatePromptDialog(rootParent, "block", args, n);
    help_button = XmSelectionBoxGetChild(blockDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(blockDialog, XmNokCallback, (XtCallbackProc) blockOKCB,
		  (XtPointer) "block");
    first_call = False;
  }
  XtManageChild(blockDialog);
}

void chunkCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];
  
  if (first_call) {
    n = 0;
    chunkDialog = XmCreatePromptDialog(rootParent, "chunk", args, n);
    help_button = XmSelectionBoxGetChild(chunkDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(chunkDialog, XmNokCallback, (XtCallbackProc) chunkOKCB,
		  (XtPointer) "chunk");
    first_call = False;
  }
  XtManageChild(chunkDialog);
}

void sBCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    sBDialog = XmCreatePromptDialog(rootParent, "sb", args, n);
    help_button = XmSelectionBoxGetChild(sBDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(sBDialog, XmNokCallback, (XtCallbackProc) sBOKCB,
		  (XtPointer) "sb");
    first_call = False;
  }
  XtManageChild(sBDialog);
}

void chanCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    chanDialog = XmCreatePromptDialog(rootParent, "chan", args, n);
    help_button = XmSelectionBoxGetChild(chanDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(chanDialog, XmNokCallback, (XtCallbackProc) chanOKCB,
		  (XtPointer) "chan");
    first_call = False;
  }
  XtManageChild(chanDialog);
}

void rxCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    rxDialog = XmCreatePromptDialog(rootParent, "rx", args, n);
    help_button = XmSelectionBoxGetChild(rxDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(rxDialog, XmNokCallback, (XtCallbackProc) rxOKCB,
		  (XtPointer) "rx");
    first_call = False;
  }
  XtManageChild(rxDialog);
}

void pointCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    pointDialog = XmCreatePromptDialog(rootParent, "point", args, n);
    help_button = XmSelectionBoxGetChild(pointDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(pointDialog, XmNokCallback, (XtCallbackProc) pointOKCB,
		  (XtPointer) "point");
    first_call = False;
  }
  XtManageChild(pointDialog);
}

void sWARMAveCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    sWARMAveDialog = XmCreatePromptDialog(rootParent, "sWARMAve", args, n);
    help_button = XmSelectionBoxGetChild(sWARMAveDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(sWARMAveDialog, XmNokCallback, (XtCallbackProc) sWARMAveOKCB,
		  (XtPointer) "sWARMAve");
    first_call = False;
  }
  XtManageChild(sWARMAveDialog);
}

void closureBaseAntCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    pointDialog = XmCreatePromptDialog(rootParent, "closureBaseAnt", args, n);
    help_button = XmSelectionBoxGetChild(pointDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(pointDialog, XmNokCallback, (XtCallbackProc) closureBaseAntOKCB,
		  (XtPointer) "closureBaseAnt");
    first_call = False;
  }
  XtManageChild(pointDialog);
}

void helpCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		cancel_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    XmString info_Xstring;

    n = 0;
    helpDialog = XmCreateInformationDialog(rootParent, "help", args, n);
    XtUnmanageChild(XtNameToWidget(helpDialog, "Help"));
    cancel_button = XmMessageBoxGetChild(helpDialog, XmDIALOG_CANCEL_BUTTON);
    XtUnmanageChild(cancel_button);
    info_Xstring = XmStringCreateSimple("corrPlotter Help");
    XtVaSetValues(helpDialog, XmNmessageString, info_Xstring, NULL);
    XmStringFree(info_Xstring);
    n = 0;
    XtSetArg(args[n], XmNeditable, False); n++;
    XtSetArg(args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
    XtSetArg(args[n], XmNrows, 12); n++;
    XtSetArg(args[n], XmNcolumns, 55); n++;
    XtSetArg(args[n], XmNscrollHorizontal, False); n++;
    helpScroll = XmCreateScrolledText(helpDialog, "helpScrolled", args, n);
    XmTextSetString(helpScroll, "corrPlotter displays the data produced by the SMA\ncorrelator.   It must be run on the same workstation\nwhich is running corrSaver, and which is listed\non the \"P Serv\" line on the c page of the curses\nmonitor.\n\ncorrPlotter has two modes: \"scan-mode\", in which\neach scan produced by the correlator is shown as\na function of frequency, and \"mir-mode\" in which\nthe amplitude and phase of all the scans in the\ncurrent data file are shown as a function of time.\n\nIn scan-mode, the correlator sends each completed\nscan to corrSaver (a daemon process), which writes\nthe data into a shared memory structure.   This\nshared memory structure is read by corrPlotter.\n\nType \"h\" for the keyboard shortcuts");
    XtManageChild(helpScroll);
    first_call = False;
  }
  XtManageChild(helpDialog);
}

void rangeCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    rangeDialog = XmCreatePromptDialog(rootParent, "range", args, n);
    help_button = XmSelectionBoxGetChild(rangeDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(rangeDialog, XmNokCallback, (XtCallbackProc) rangeOKCB,
		  (XtPointer) "range");
    first_call = False;
  }
  XtManageChild(rangeDialog);
}

void timeRangeCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    timeRangeDialog = XmCreatePromptDialog(rootParent, "timeRange", args, n);
    help_button = XmSelectionBoxGetChild(timeRangeDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(timeRangeDialog, XmNokCallback, (XtCallbackProc) timeRangeOKCB,
		  (XtPointer) "timeRange");
    first_call = False;
  }
  XtManageChild(timeRangeDialog);
}

void testCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    testDialog = XmCreatePromptDialog(rootParent, "test", args, n);
    help_button = XmSelectionBoxGetChild(testDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(testDialog, XmNokCallback, (XtCallbackProc) testOKCB,
		  (XtPointer) "range");
    first_call = False;
  }
  XtManageChild(testDialog);
}

void pauseCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];

  if (first_call) {
    n = 0;
    pauseDialog = XmCreatePromptDialog(rootParent, "pause", args, n);
    help_button = XmSelectionBoxGetChild(pauseDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(pauseDialog, XmNokCallback, (XtCallbackProc) pauseOKCB,
		  (XtPointer) "pause");
    first_call = False;
  }
  XtManageChild(pauseDialog);
}

void printFilters(void) {
  int i, j;

  printf("activeRx = %d\n", activeRx);
  printf("startScan = %d, endScan = %d, selectedSourceType = %d\n", startScan, endScan, selectedSourceType);
  printf("startTime = %f, endTime = %f\n", startTime, endTime);
  printf("bslnFilter = \"%s\", nChunks = %d\n", bslnFilter, nChunks);
  printf("requestedBaselines:\n");
  for (i = 0; i < 11; i++) {
    printf("%d: ", i);
    for (j = 0; j < 11; j++)
      printf("%d ", j);
    printf("\n");
  }
  printf("ChunkList: ");
  for (i = 0; i < N_CHUNKS; i++)
    printf("%d ", chunkList[i]);
  printf("\n");
  printf("nSidebands %d sBList[0] %d sBList[1] %d sBFilter: \"%s\" sourceFilter: \"%s\", currentSource %d\n",
	 nSidebands, sBList[0], sBList[1], sBFilter, sourceFilter, currentSource);
  printf("blockFilter \"%s\" chunkFilter \"%s\"\n", blockFilter, chunkFilter);
  printf("plotOneBlockOnly %d nBlockRequested %d\n", plotOneBlockOnly, nBlocksRequested);
  printf("requestedBlockList: ");
  for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
    printf("%d ", requestedBlockList[i]);
  printf("\n");
}

void saveRestoreFilters(int save)
{
  static int sActiveRx, sEndScan, sRequestedBaselines[11][11], i, j, sNBlocksRequested;
  static int sNChunks, sSBList[2], sCurrentSource, sBlackListedSource[500], sPlotOneBlockOnly;
  static int sRequestedBlockList[N_IFS*N_BLOCKS + 1], sChunkList[10], sNSidebands;
  static char sBslnFilter[80], sBlockFilter[80], sChunkFilter[80], sSBFilter[80], sSourceFilter[80];

  if (save) {
    sActiveRx = activeRx;
    sEndScan = endScan;
    strcpy(sBslnFilter, bslnFilter);
    for (i = 0; i < 11; i++)
      for (j = 0; j < 11; j++)
	sRequestedBaselines[i][j] = requestedBaselines[i][j];
    sNChunks = nChunks;
    for (i = 0; i < N_CHUNKS; i++)
      sChunkList[i] = chunkList[i];
    sNSidebands = nSidebands;
    for (i = 0; i < 2; i++)
      sSBList[i] = sBList[i];
    strcpy(sSBFilter, sBFilter);
    strcpy(sSourceFilter, sourceFilter);
    strcpy(sBlockFilter, blockFilter);
    strcpy(sChunkFilter, chunkFilter);
    sCurrentSource = currentSource;
    bcopy(blackListedSource, sBlackListedSource, 500*sizeof(int));
    sPlotOneBlockOnly = plotOneBlockOnly;
    sNBlocksRequested = nBlocksRequested;
    for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
      sRequestedBlockList[i] = requestedBlockList[i];
  } else {
    activeRx = sActiveRx;
    endScan = sEndScan;
    strcpy(bslnFilter, sBslnFilter);
    for (i = 0; i < 11; i++)
      for (j = 0; j < 11; j++)
	requestedBaselines[i][j] = sRequestedBaselines[i][j];
    nChunks = sNChunks;
    for (i = 0; i < N_CHUNKS; i++)
      chunkList[i] = sChunkList[i];
    nSidebands = sNSidebands;
    for (i = 0; i < 2; i++)
      sBList[i] = sSBList[i];
    strcpy(sBFilter, sSBFilter);
    strcpy(sourceFilter, sSourceFilter);
    currentSource = sCurrentSource;
    strcpy(blockFilter, sBlockFilter);
    strcpy(chunkFilter, sChunkFilter);
    bcopy(sBlackListedSource, blackListedSource, 500*sizeof(int));
    plotOneBlockOnly = sPlotOneBlockOnly;
    nBlocksRequested = sNBlocksRequested;
    for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
      requestedBlockList[i] = sRequestedBlockList[i];
  }
}

void resetFilters(void)
{
  int i, j;

  activeRx = 1;
  startScan = endScan = selectedSourceType = -1;
  startTime = endTime = -1.0;
  sprintf(bslnFilter, "*-*");
  for (i = 0; i < 11; i++)
    for (j = 0; j < 11; j++)
      requestedBaselines[i][j] = 1;
  nChunks = 4;
  for (i = 0; i < N_CHUNKS; i++)
    chunkList[i] = i;
  nSidebands = 2;
  sBList[0] = 0;
  sBList[1] = 1;
  sBFilter[0] = '*';
  sprintf(sourceFilter, "*");
  currentSource = -1;
  bzero(blackListedSource, 500*sizeof(int));
  sprintf(blockFilter, "*");
  sprintf(chunkFilter, "*");
  plotOneBlockOnly = FALSE;
  nBlocksRequested = N_BLOCKS;
  for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
    requestedBlockList[i] = 1;
}

void polRadioButtonCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  int startPolMask;

  startPolMask = polMask;
  if (widget == polAllWidget)
    polMask = POL_SHOW_00 | POL_SHOW_RR | POL_SHOW_LL | POL_SHOW_RL | POL_SHOW_LR | POL_SHOW_VV | POL_SHOW_HH | POL_SHOW_VH | POL_SHOW_HV;
  else if (widget == polRRLLWidget)
    polMask = POL_SHOW_RR | POL_SHOW_LL;
  else if (widget == polRRWidget)
    polMask = POL_SHOW_RR;
  else if (widget == polLLWidget)
    polMask = POL_SHOW_LL;
  else if (widget == polRLLRWidget)
    polMask = POL_SHOW_RL | POL_SHOW_LR;
  else if (widget == polRLWidget)
    polMask = POL_SHOW_RL;
  else if (widget == polLRWidget)
    polMask = POL_SHOW_LR;
  else if (widget == polVVHHWidget)
    polMask = POL_SHOW_VV | POL_SHOW_HH;
  else if (widget == polVVWidget)
    polMask = POL_SHOW_VV;
  else if (widget == polHHWidget)
    polMask = POL_SHOW_HH;
  else if (widget == polVHHVWidget)
    polMask = POL_SHOW_VH | POL_SHOW_HV;
  else if (widget == polVHWidget)
    polMask = POL_SHOW_VH;
  else if (widget == polHVWidget)
    polMask = POL_SHOW_HV;
  if (startPolMask != polMask)
    forceRedraw("polRadioButtonCB");
}

void radioButtonCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  bzero(blackListedSource, 500*sizeof(int));
  if (widget == allSourcesWidget)
    selectedSourceType = -1;
  else if (widget == fluxSourcesWidget)
    selectedSourceType = 0x01;
  else if (widget == gainSourcesWidget)
    selectedSourceType = 0x04;
  else if (widget == bandpassSourcesWidget)
    selectedSourceType = 0x02;
  else if (widget == scienceSourcesWidget)
    selectedSourceType = 0;
  else
    fprintf(stderr, "radioButtonCB called with unknown widget\n");
  forceRedraw("radioButtonCB");
}

void polTypeCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  Cardinal n = 0;
  Arg        args[20];

  if (polRadioBox == 0) {
    polRadioBoxContainer = XmCreateBulletinBoardDialog(rootParent, "Select Pol. State",
						       args, n);
    polRadioBox = XmCreateRadioBox (polRadioBoxContainer, "Pol. State", args, n);
    n = 0;
    XtSetArg(args[n], XmNset, True);  n++;
    polAllWidget = XmCreateToggleButton(polRadioBox, "All", args, n);
    XtAddCallback(polAllWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polAllWidget);
    n = 0;
    XtSetArg(args[n], XmNset, False);  n++;
    polRRLLWidget = XmCreateToggleButton(polRadioBox, "RR + LL", args, n);
    XtAddCallback(polRRLLWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polRRLLWidget);

    polRRWidget = XmCreateToggleButton(polRadioBox, "RR", args, n);
    XtAddCallback(polRRWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polRRWidget);

    polLLWidget = XmCreateToggleButton(polRadioBox, "LL", args, n);
    XtAddCallback(polLLWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polLLWidget);

    polRLLRWidget = XmCreateToggleButton(polRadioBox, "RL + LR", args, n);
    XtAddCallback(polRLLRWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polRLLRWidget);

    polRLWidget = XmCreateToggleButton(polRadioBox, "RL", args, n);
    XtAddCallback(polRLWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polRLWidget);

    polLRWidget = XmCreateToggleButton(polRadioBox, "LR", args, n);
    XtAddCallback(polLRWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polLRWidget);

    polVVHHWidget = XmCreateToggleButton(polRadioBox, "VV + HH", args, n);
    XtAddCallback(polVVHHWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polVVHHWidget);

    polVVWidget = XmCreateToggleButton(polRadioBox, "VV", args, n);
    XtAddCallback(polVVWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polVVWidget);

    polHHWidget = XmCreateToggleButton(polRadioBox, "HH", args, n);
    XtAddCallback(polHHWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polHHWidget);

    polVHHVWidget = XmCreateToggleButton(polRadioBox, "VH + HV", args, n);
    XtAddCallback(polVHHVWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polVHHVWidget);

    polVHWidget = XmCreateToggleButton(polRadioBox, "VH", args, n);
    XtAddCallback(polVHWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polVHWidget);

    polHVWidget = XmCreateToggleButton(polRadioBox, "HV", args, n);
    XtAddCallback(polHVWidget, XmNvalueChangedCallback, polRadioButtonCB, call_data);
    XtManageChild(polHVWidget);

    XtManageChild(polRadioBox);
  }
  XtManageChild(polRadioBoxContainer);
  forceRedraw("polTypeCB");
}

void typeCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  Cardinal n = 0;
  Arg	     args[20];
  
  if (typeRadioBox == 0) {
    radioBoxContainer = XmCreateBulletinBoardDialog(rootParent, "Select Source Type",
						    args, n);
    typeRadioBox = XmCreateRadioBox (radioBoxContainer, "Type", args, n);
    n = 0;
    XtSetArg(args[n], XmNset, True);  n++;
    allSourcesWidget = XmCreateToggleButton(typeRadioBox, "All", args, n);
    XtAddCallback(allSourcesWidget, XmNvalueChangedCallback, radioButtonCB, call_data);
    XtManageChild(allSourcesWidget);
    n = 0;
    XtSetArg(args[n], XmNset, False);  n++;
    fluxSourcesWidget = XmCreateToggleButton(typeRadioBox, "Flux Cal.", args, n);
    XtAddCallback(fluxSourcesWidget, XmNvalueChangedCallback, radioButtonCB, call_data);
    XtManageChild(fluxSourcesWidget);
    gainSourcesWidget = XmCreateToggleButton(typeRadioBox, "Gain Cal.", args, n);
    XtAddCallback(gainSourcesWidget, XmNvalueChangedCallback, radioButtonCB, call_data);
    XtManageChild(gainSourcesWidget);
    bandpassSourcesWidget = XmCreateToggleButton(typeRadioBox, "Bandpass Cal.", args, n);
    XtManageChild(bandpassSourcesWidget);
    XtAddCallback(bandpassSourcesWidget, XmNvalueChangedCallback, radioButtonCB, call_data);
    scienceSourcesWidget = XmCreateToggleButton(typeRadioBox, "Science", args, n);
    XtAddCallback(scienceSourcesWidget, XmNvalueChangedCallback, radioButtonCB, call_data);
    XtManageChild(scienceSourcesWidget);
    XtManageChild(typeRadioBox);
  }
  XtManageChild(radioBoxContainer);
  forceRedraw("typeCB");
}

void rFCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  resetFilters();
  forceRedraw("rFCB");
}

Widget CreateToggleButton(Widget parent, char* name, XtCallbackProc callback,
    XtPointer client_data)
{
    Widget toggle;

    toggle = XtVaCreateManagedWidget(name, xmToggleButtonWidgetClass, parent,
	XmNindicatorType, XmN_OF_MANY, NULL);
    XtAddCallback(toggle, XmNvalueChangedCallback, callback, client_data);
    return toggle;
}

void toggleCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int firstCall = TRUE;
  static Widget ampWarning, cohWarning;
  XmToggleButtonCallbackStruct* ptr;

  if (firstCall) {
    Cardinal n = 0;
    Arg	     args[20];
    Widget   cancel_button, help_button;
    XmString ampWhine, cohWhine;

    ampWarning = XmCreateErrorDialog(rootParent, "Amplitude Warning",
				     args, n);
    cancel_button = XmMessageBoxGetChild(ampWarning, XmDIALOG_CANCEL_BUTTON);
    XtUnmanageChild(cancel_button);
    help_button = XmMessageBoxGetChild(ampWarning, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(ampWarning, XmNokCallback, 
		  (void *)XtUnmanageChild, NULL);
    ampWhine=XmStringCreateLocalized("Amplitude will no longer be plotted");
    XtVaSetValues(ampWarning, XmNmessageString, ampWhine, NULL);
    cohWarning = XmCreateErrorDialog(rootParent, "Coherence Warning",
				     args, n);
    cancel_button = XmMessageBoxGetChild(cohWarning, XmDIALOG_CANCEL_BUTTON);
    XtUnmanageChild(cancel_button);
    help_button = XmMessageBoxGetChild(cohWarning, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(cohWarning, XmNokCallback, 
		  (void *)XtUnmanageChild, NULL);
    cohWhine=XmStringCreateLocalized("Coherence will no longer be plotted");
    XtVaSetValues(cohWarning, XmNmessageString, cohWhine, NULL);
    firstCall = FALSE;
  }
  ptr = (XmToggleButtonCallbackStruct *) call_data;
  if (ptr != NULL) {
    if (strcmp((char *)client_data, "checkStatistics") == 0)
      if (ptr->set == TRUE)
	checkStatistics = TRUE;
      else
	checkStatistics = FALSE;
    else if (strcmp((char *)client_data, "negateBsln") == 0)
      if (ptr->set == TRUE)
	negateBsln = TRUE;
      else
	negateBsln = FALSE;
    else if (strcmp((char *)client_data, "debugMessages") == 0)
      if (ptr->set == TRUE)
	debugMessagesOn = TRUE;
      else
	debugMessagesOn = FALSE;
    else if (strcmp((char *)client_data, "showAmp") == 0)
      if (ptr->set == TRUE) {
	showAmp = TRUE;
	showLags = FALSE;
	showClosure = FALSE;
	XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	if (showCoh) {
	  XmToggleButtonSetState(cohToggle, FALSE, FALSE);
	  showCoh = FALSE;
	  XtManageChild(cohWarning);
	}
      } else
	showAmp = FALSE;
    else if (strcmp((char *)client_data, "lo") == 0) {
      activeRx = LOW_RX_CODE;
      if (doubleBandwidth)
	sprintf(rxFilter, "Low Freq. Rx");
      else
	sprintf(rxFilter, "Lower 2 GHz ");
      XmToggleButtonSetState(hiToggle, FALSE, FALSE);
    } else if (strcmp((char *)client_data, "hi") == 0) {
      activeRx = HIGH_RX_CODE;
      if (doubleBandwidth)
	sprintf(rxFilter, "Upper 2 GHz  ");
      else
	sprintf(rxFilter, "High Freq. Rx");
      XmToggleButtonSetState(loToggle, FALSE, FALSE);
    } else if (strcmp((char *)client_data, "showCoh") == 0)
      if (ptr->set == TRUE) {
	showCoh = TRUE;
	showClosure = FALSE;
	XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	if (showAmp) {
	  showLags = FALSE;
	  XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	  XmToggleButtonSetState(ampToggle, FALSE, FALSE);
	  showAmp = FALSE;
	  XtManageChild(ampWarning);
	}
      } else
	showCoh = FALSE;
    else if (strcmp((char *)client_data, "showPhase") == 0)
      if (ptr->set == TRUE) {
	showLags = FALSE;
	XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	showClosure = FALSE;
	XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	showPhase = TRUE;
      } else
	showPhase = FALSE;
    else if (strcmp((char *)client_data, "showLags") == 0)
      if (ptr->set == TRUE) {
	showClosure = FALSE;
	XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	XmToggleButtonSetState(lagsToggle, TRUE, FALSE);
	showLags = TRUE;
	XmToggleButtonSetState(phaseToggle, FALSE, FALSE);
	showPhase = FALSE;
	XmToggleButtonSetState(ampToggle, FALSE, FALSE);
	showAmp = FALSE;
	XmToggleButtonSetState(cohToggle, FALSE, FALSE);
	showCoh = FALSE;
      } else {
	showLags = FALSE;
	XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	showPhase = TRUE;
	XmToggleButtonSetState(phaseToggle, TRUE, FALSE);
	showAmp = TRUE;
	XmToggleButtonSetState(ampToggle, TRUE, FALSE);
	showCoh = FALSE;
	XmToggleButtonSetState(ampToggle, FALSE, FALSE);
      }
    else if (strcmp((char *)client_data, "autoscaleAmplitude") == 0)
      if (ptr->set == TRUE)
	autoscaleAmplitude = TRUE;
      else
	autoscaleAmplitude = FALSE;
    else if (strcmp((char *)client_data, "logPlotAutos") == 0)
      if (ptr->set == TRUE)
	sWARMLogPlot = TRUE;
      else
	sWARMLogPlot = FALSE;
    else if (strcmp((char *)client_data, "linePlotAutos") == 0)
      if (ptr->set == TRUE)
	sWARMLinePlot = TRUE;
      else
	sWARMLinePlot = FALSE;
    else if (strcmp((char *)client_data, "plotSWARM") == 0)
      if (ptr->set == TRUE)
	shouldPlotSWARM = TRUE;
      else
	shouldPlotSWARM = FALSE;
    else if (strcmp((char *)client_data, "bandLabeling") == 0) {
      if (ptr->set == TRUE)
	bandLabeling = TRUE;
      else
	bandLabeling = FALSE;
      newPoints = TRUE; /* Force a redraw */
    } else if (strcmp((char *)client_data, "scan mode") == 0) {
      if ((ptr->set == TRUE) && (corrSaverMachine))
	scanMode  = TRUE;
      else
	scanMode = FALSE;
      havePlottedSomething = FALSE;
      setSensitivities();
    } else if (strcmp((char *)client_data, "plotFromZero") == 0)
      if (ptr->set == TRUE)
	plotFromZero = TRUE;
      else
	plotFromZero = FALSE;
    else if (strcmp((char *)client_data, "useFullCatalog") == 0)
      if (ptr->set == TRUE)
	useFullCatalog = TRUE;
      else
	useFullCatalog = FALSE;
    else if (strcmp((char *)client_data, "plotToOne") == 0)
      if (ptr->set == TRUE)
	plotToOne = TRUE;
      else
	plotToOne = FALSE;
    else if (strcmp((char *)client_data, "logPlot") == 0)
      if (ptr->set == TRUE)
	logPlot = TRUE;
      else
	logPlot = FALSE;
    else if (strcmp((char *)client_data, "scaleInMHz") == 0)
      if (ptr->set == TRUE)
	scaleInMHz = TRUE;
      else
	scaleInMHz = FALSE;
    else if (strcmp((char *)client_data, "integrate") == 0)
      if (ptr->set == TRUE) {
	FILE *projectFile;

	projectFile = fopen("/sma/rtdata/engineering/monitorLogs/littleLog.txt", "r");
	if (projectFile != NULL) {
	  fscanf(projectFile, "%s", &integrateSource[0]);
	  fclose(projectFile);
	} else
	  integrateSource[0] = (char)0;
	integrate = TRUE;
      } else
	integrate = FALSE;
    else if (strcmp((char *)client_data, "showGood") == 0)
      if (ptr->set == TRUE)
	showGood = TRUE;
      else
	showGood = FALSE;
    else if (strcmp((char *)client_data, "showBad") == 0)
      if (ptr->set == TRUE)
	showBad = TRUE;
      else
	showBad = FALSE;
    else if (strcmp((char *)client_data, "showRefresh") == 0)
      if (ptr->set == TRUE)
	showRefresh = TRUE;
      else
	showRefresh = FALSE;
    else if (strcmp((char *)client_data, "freeze") == 0)
      if (ptr->set == TRUE) {
	XCopyGC(myDisplay, blueGc, 0x7fffff, labelGc);
	disableUpdates = TRUE;
      } else {
	XCopyGC(myDisplay, whiteGc, 0x7fffff, labelGc);
	disableUpdates = FALSE;
      }
    else if (strcmp((char *)client_data, "closure") == 0)
      if (ptr->set == TRUE) {
	showClosure = TRUE;
	showPhase = TRUE;
	XmToggleButtonSetState(phaseToggle, FALSE, FALSE);
	if (showAmp) {
	  showLags = FALSE;
	  XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	  XmToggleButtonSetState(ampToggle, FALSE, FALSE);
	  showAmp = FALSE;
	  XtManageChild(ampWarning);
	}
	if (showCoh) {
	  XmToggleButtonSetState(cohToggle, FALSE, FALSE);
	  showCoh = FALSE;
	  XtManageChild(cohWarning);
	}
      } else {
	showClosure = FALSE;
	XmToggleButtonSetState(phaseToggle, TRUE, FALSE);
      }
    else if (strcmp((char *)client_data, "bslnOrder") == 0)
      if (ptr->set == TRUE) {
	bslnOrder = TRUE;
      } else {
	gotBslnLength = FALSE;
	bslnOrder = FALSE;
      }
    else if (strcmp((char *)client_data, "wrapColor") == 0)
      if (ptr->set == TRUE)
	wrapColor = TRUE;
      else
	wrapColor = FALSE;
    else if (strcmp((char *)client_data, "timePlot") == 0)
      if (ptr->set == TRUE)
	timePlot = TRUE;
      else
	timePlot = FALSE;
    else if (strcmp((char *)client_data, "hAPlot") == 0)
      if (ptr->set == TRUE)
	timePlot = hAPlot = TRUE;
      else
	hAPlot = FALSE;
    else if (strcmp((char *)client_data, "applySecantZ") == 0)
      if (ptr->set == TRUE)
	applySecantZ = TRUE;
      else
	applySecantZ = FALSE;
    else if (strcmp((char *)client_data, "grid") == 0) {
      if (ptr->set == TRUE)
	grid = TRUE;
      else
	grid = FALSE;
    }
    forceRedraw("toggleCB");
  }
}

void bslnCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  static int		first_call = True;
  Widget		help_button;
  Cardinal		n;
  Arg			args[20];
  
  if (first_call) {
    n = 0;
    bslnDialog = XmCreatePromptDialog(rootParent, "bsln", args, n);
    help_button = XmSelectionBoxGetChild(bslnDialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(help_button);
    XtAddCallback(bslnDialog, XmNokCallback, (XtCallbackProc) bslnOKCB,
		  (XtPointer) "bsln");
    negateBsln = FALSE;
    negBslnToggle = CreateToggleButton(bslnDialog, "Use all but these", toggleCB, "negateBsln");
    XmToggleButtonSetState(negBslnToggle, FALSE, FALSE);
    first_call = False;
  }
  XtManageChild(bslnDialog);
}

void refreshDisplay()
{
  if (!showRefresh)
    XCopyArea(myDisplay, pixmap, myWindow, whiteGc, 0, 0, displayWidth, displayHeight, 0, 0);
  else {
    if (scanMode)
      redrawScreen();
  else
    redrawScreenTrack();
  }
}

int nearestLine(lineFreqKey *index, int nEntries, double value)
{
  int size;
  int ptr;

  size = nEntries/2;
  ptr = size;
  while (size > 0) {
    dprintf("ptr = %d, size = %d, frequency = %f value = %f\n",
	   ptr, size, index[ptr].ptr->frequency, value);
    if (size > 2)
      size = size/2 + (size % 2);
    else
      size /= 2;
    if (index[ptr].ptr->frequency > value)
      ptr -= size;
    else
      ptr += size;
  }
  if ((ptr == nEntries) || (ptr == 0))
    return(ptr);
  else if (fabs(index[ptr-1].ptr->frequency - value) < fabs(index[ptr].ptr->frequency - value))
    return(ptr-1);
  else if (fabs(index[ptr+1].ptr->frequency - value) < fabs(index[ptr].ptr->frequency - value))
    return(ptr+1);
  else
    return(ptr);
}

void markCallback()
{
  static markListEntry *lastEntry;
  markListEntry *ptr, *newEntry;

  dprintf("I'm in markCallback\n");
  newEntry = (markListEntry *)malloc(sizeof(markListEntry));
  if (newEntry == NULL) {
    perror("malloc of new markListEntry");
    exit(-1);
  }
  newEntry->frequency = transitionFreq;
  newEntry->offset = transitionOffset;
  newEntry->sB = transitionSB;
  newEntry->band = transitionBand;
  newEntry->name = malloc(strlen(transitionName)+1);
  if (newEntry->name == NULL) {
    perror("malloc of makeListEntry name");
    exit(-1);
  }
  strcpy(newEntry->name, transitionName);
  newEntry->next = NULL;
  if (markListRoot == NULL) {
    markListRoot = newEntry;
    newEntry->last = NULL;
  } else {
    lastEntry->next = newEntry;
    newEntry->last = lastEntry;
  }
  lastEntry = newEntry;
  ptr = markListRoot;
  while (ptr != NULL) {
    dprintf("Mark \"%s\" at %f\n", ptr->name, ptr->frequency);
    ptr = ptr->next;
  }
}

void unmarkCallback()
{
  markListEntry *ptr, *last;

  if (markListRoot != NULL) {
    ptr = markListRoot;

    while (ptr->next != NULL) {
      ptr = ptr->next;
    }
    while (ptr != NULL) {
      last = ptr;
      ptr = ptr->last;
      free(last->name);
      free(last);
    }
    markListRoot = NULL;
  }
}

void inputCB(Widget widget, XtPointer client_data, XtPointer call_data)
{
  int x, y, i, j;
  int found = FALSE;
  static int inXAxisSelect = FALSE;
  static float newXStart, newXEnd;
  XmDrawingAreaCallbackStruct* ptr;
  XEvent* Xptr;
  cell *nextCell;
  label *nextLabel;

  ptr = (XmDrawingAreaCallbackStruct *) call_data;
  Xptr = ptr->event;
  x = (*Xptr).xbutton.x;
  y = (*Xptr).xbutton.y;
  /* button = (*Xptr).xbutton.button; */
  if (ptr->event->type == ButtonRelease) {
    if (inXAxisSelect) {
      float xAxisValue;
      
      if (x >= displayWidth)
	if (timePlot)
	  xAxisValue = -100.0;
	else
	  xAxisValue = -1.0;
      else
	xAxisValue = ((float)(x - timeAxisA))*timeAxisM + timeAxisB;
      newXEnd = xAxisValue;
      if (newXStart != newXEnd) {
	if ((newXStart > newXEnd) && (newXEnd != -100.0)) {
	  float dummy;
	  
	  dummy = newXEnd;
	  newXEnd = newXStart;
	  newXStart = dummy;
	}
	if (timePlot) {
	  startTime = newXStart;
	  endTime = newXEnd;
	} else {
	  startScan = newXStart;
	  endScan = newXEnd;
	}
	havePlottedSomething = FALSE;
	shouldSayRedrawing = TRUE;
	forceRedraw("inputCB");
      }
      inXAxisSelect = FALSE;
    }
  } else if (ptr->event->type == ButtonPress) {
    int button;

    button = (*Xptr).xbutton.button;
    if ((trackFileVersion < 2) && (!scanMode) && (y >= yTickTop) && (x >= xStartScan)) {
      int selected;
      float fS, fE, fScale;

      fS = (float)xStartScanNumber;
      fE = (float)xEndScanNumber;
      fScale = (fE - fS) / (float)xScanRange;
      selected = (int)(fScale * ((float)x - (float)xStartScan));
      selected += xStartScanNumber;
      selected = 10 * (selected / 10);
      if ((button == 1) && (selected >= 0))
	startScan = selected;
      else if ((button == 3) && (selected <= xEndScanNumber))
	endScan = selected;
    } else if (zoomed && (scanMode || (y > 30))) {
      if (button != 3) {
	for (i = 0; i < 11; i++)
	  for (j = 0; j < 11; j++)
	    requestedBaselines[i][j] = savedRequestedBaselines[i][j];
	nBlocksRequested = savedNBlocksRequested;
	for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
	  requestedBlockList[i] = savedRequestedBlockList[i];
	nChunks = savedNChunks;
	for (i = 0; i < N_CHUNKS; i++)
	  chunkList[i] = savedChunkList[i];
	nSidebands = savedNSidebands;
	for (i = 0; i < 2; i++)
	  sBList[i] = savedSBList[i];
	activeRx = savedActiveRx;
	strcpy(bslnFilter, savedBslnFilter);
	strcpy(blockFilter, savedBlockFilter);
	strcpy(chunkFilter, savedChunkFilter);
	strcpy(sBFilter, savedSBFilter);
	strcpy(sourceFilter, savedSourceFilter);
	currentSource = savedCurrentSource;
	leftMargin = DEFAULT_LEFT_MARGIN;
	rightMargin = ZOOMED_RIGHT_MARGIN;
	if (bandLabeling) {
	  topMargin = SMALL_TOP_MARGIN;
	  blockSkip = SMALL_BLOCK_SKIP;
	} else {
	  topMargin = DEFAULT_TOP_MARGIN;
	  blockSkip = DEFAULT_BLOCK_SKIP;
	}
	bottomMargin = DEFAULT_BOTTOM_MARGIN;
	havePlottedSomething = FALSE;
	shouldSayRedrawing = TRUE;
	sayRedrawing();
	zoomed = sWARMZoomed = FALSE;
      } else { /* button == 3 */
	static int firstCall = TRUE;
	static Widget infoWidget;
	int dSMStatus;
	char information[1000], rxString[5];
	Cardinal		n;
	Arg			args[20];
	Widget   cancel_button, help_button;
	static Widget mark_button, unmark_button;
	XmString ampWhine;

	if (firstCall) {
	  n = 0;
	  infoWidget = XmCreateInformationDialog(rootParent, "Cell Info",
						 args, n);
	  cancel_button = XmMessageBoxGetChild(infoWidget, XmDIALOG_CANCEL_BUTTON);
	  XtUnmanageChild(cancel_button);
	  help_button = XmMessageBoxGetChild(infoWidget, XmDIALOG_HELP_BUTTON);
	  XtUnmanageChild(help_button);
	  XtAddCallback(infoWidget, XmNokCallback, 
			(void *)XtUnmanageChild, NULL);
	  unmark_button = XmCreatePushButton(infoWidget, "Clear Markers", NULL, 0);
	  XtAddCallback(unmark_button, XmNactivateCallback, unmarkCallback, (XtPointer) "unmark");
	  XtManageChild(unmark_button);
	  firstCall = FALSE;
	} else
	  XtUnmanageChild(mark_button);
	if ((x > leftMargin) && (x < displayWidth-rightMargin) &&
	    (y > topMargin) && (y < displayHeight-bottomMargin)) {
	  int channel, ii, jj, sB, block, chunk, sBand, lineIndex;
	  int bestChunk, bestSB;
	  double testFrequency, closest;
	  float fOffset;
	  double chunkFrequencies[2][2][24], chunkVelocities[2][2][24], velocity, frequency,
	    fRest;
	  char direction[10];
	  time_t timestamp;

	  if (!plotInfoInitialized) {
	    dSMStatus = dsm_open();
	    if (dSMStatus != DSM_SUCCESS) {
	      dsm_error_message(dSMStatus, "dsm_open");
	      exit(-1);
	    }
 	    dSMStatus = dsm_structure_init(&plotInfo, "PLOT_FREQ_INFO_X");
	    if (dSMStatus != DSM_SUCCESS) {
	      dsm_error_message(dSMStatus, "dsm_structure_init");
	      exit(-1);
	    }
	    plotInfoInitialized = TRUE;
	  }
	  dSMStatus = dsm_read("m5", "PLOT_FREQ_INFO_X", &plotInfo, &timestamp);
	  if (dSMStatus != DSM_SUCCESS) {
	    perror("dsm_read(\"m5\", \"PLOT_FREQ_INFO_X\", &plotInfo, &timestamp);");
	    exit(-1);
	  }
	  dSMStatus = dsm_structure_get_element(&plotInfo,
						"CHUNK_FREQUENCIES_V2_V2_V24_D",
						(char *)chunkFrequencies);
	  if (dSMStatus != DSM_SUCCESS)
	    perror("get_element CHUNK_FREQUENCIES_V2_V2_V24_D");
	  dSMStatus = dsm_structure_get_element(&plotInfo,
						"CHUNK_VRADIAL_V2_V2_V24_D",
						(char *)chunkVelocities);
	  if (dSMStatus != DSM_SUCCESS)
	    perror("get_element CHUNK_VRADIAL_V2_V2_V24_D");
	  if (activeRx == HIGH_RX_CODE)
	    sprintf(rxString, "High");
	  else
	    sprintf(rxString, "Low");
	  channel = (int)(((float)(x-cellX0)/cellInc)+0.5);
	  fOffset = (((float)(channel-(cellNChannels/2 - 1)))/((float)cellNChannels))*104.0e6;
	  if (sBFilter[0] == 'L')
	    sB = 0;
	  else
	    sB = 1;
	  if (sB == 0)
	    fOffset *= -1.0;
	  sscanf(blockFilter, "%d", &block);
	  sscanf(chunkFilter, "%d", &chunk);
	  sBand = 4*(block-1);
	  if (block > 3)
	    sBand += 5 - chunk;
	  else
	    sBand += chunk;
	  sBand -= 1;
	  frequency = chunkFrequencies[1-activeRx][sB][sBand] + fOffset;
	  velocity = chunkVelocities[1-activeRx][sB][sBand];
	  fRest = frequency*(1.0+(velocity/SPEED_OF_LIGHT));
	  if (useFullCatalog) {
	    lineIndex = nearestLine(fullFreqIndex, nLineEntries, fRest*1.0e-9);
	    if (fullFreqIndex[lineIndex].ptr != NULL) {
	      strcpy(transitionName, fullFreqIndex[lineIndex].ptr->fullName);
	      transitionFreq = fullFreqIndex[lineIndex].ptr->frequency;
	    } else {
	      strcpy(transitionName, "????");
	      transitionFreq = 0.0;
	    }
	  } else {
	    lineIndex = nearestLine(shortFreqIndex, nNamedLines, fRest*1.0e-9);
	    if (shortFreqIndex[lineIndex].ptr != NULL) {
	      strcpy(transitionName, shortFreqIndex[lineIndex].ptr->nickName);
	      transitionFreq = shortFreqIndex[lineIndex].ptr->frequency;
	    } else {
	      strcpy(transitionName, "????");
	      transitionFreq = 0.0;
	    }
	  }
	  closest = 1.0e30;
	  for (ii = 0; ii < 2; ii++)
	    for (jj = 0; jj < 24; jj++) {
	      testFrequency = chunkFrequencies[1-activeRx][ii][jj]*(1.0+(velocity/SPEED_OF_LIGHT));
	      if (fabs(testFrequency-transitionFreq*1.0e9) < fabs(closest)) {
		closest = testFrequency-transitionFreq*1.0e9;
		bestSB = ii;
		bestChunk = jj;
	      }
	    }
	  dprintf("Nearest line: \"%s\" (index = %d)\n", transitionName, lineIndex);
	  transitionSB = bestSB;
	  transitionBand = bestChunk+1;
	  transitionOffset = closest;
	  if (transitionFreq*1.0e9 - fRest > 0.0)
	    strcpy(direction, "higher");
	  else
	    strcpy(direction, "lower");
	  sprintf(information, "Channel = %d Frequency Offest = %f MHz\nAmp = %f Phase = %f\nFsky = %8.4f GHz Frest = %8.4f GHz\nVradial at reference chunk %5.2f km/sec\nNearest spectral line %s, which is %5.3f MHz %s\nThe line will appear %f MHz from the center of chunk s%02d",
		  channel, fOffset*1.0e-6,
		  cellAmp[channel], cellPhase[channel], frequency*1.0e-9, fRest*1.0e-9, velocity*1.0e-3,
		  transitionName, fabs(transitionFreq*1.0e9 - fRest)*1.0e-6, direction, closest*1.0e-6, bestChunk+1);
	} else
	  sprintf(information, "You must have the mouse within the histogram to use this feature");
	ampWhine=XmStringCreateLocalized(information);
	XtVaSetValues(infoWidget, XmNmessageString, ampWhine, NULL);
	XmStringFree(ampWhine);
	if (fabs(transitionOffset) < 52.0e6) {
	  sprintf(information, "Mark %s", transitionName);
	  mark_button = XmCreatePushButton(infoWidget, information, NULL, 0);
	  XtAddCallback(mark_button, XmNactivateCallback, markCallback, (XtPointer) "mark");
	  XtManageChild(mark_button);
	}
	XtManageChild(infoWidget);
      }
    } else {
      /*
	First check to see if the click was in a cell
      */
      lock_cell("CB");
      nextCell = cellRoot;
      while ((nextCell != NULL) && (!found)) {
	if ((nextCell->tlcx < x) && (nextCell->tlcy < y) &&
	    (nextCell->trcx > x) && (nextCell->trcy < y) &&
	    (nextCell->brcx > x) && (nextCell->brcy > y) &&
	    (nextCell->blcx < x) && (nextCell->blcy > y)) {
	  havePlottedSomething = FALSE;
	  dprintf("Found %d-%d IF %d SB %d block %d chunk %d\n",
		 nextCell->ant1,
		 nextCell->ant2,
		 nextCell->iEf,
		 nextCell->sb,
		 nextCell->block,
		 nextCell->chunk);
	  found = TRUE;
	} else
	  nextCell = (cell *)nextCell->next;
      }
      if (found && (nextCell->ant1 == nextCell->ant2) && (nextCell->block != TIME_LABEL)) {
	zoomed = TRUE;
	zoomedAnt = nextCell->ant1;
      } else if (found && (!showClosure)) {
	if (nextCell->block == TIME_LABEL) {
	  float xAxisValue;

	  dprintf("Doing TIME_LABEL processing\n");
	  xAxisValue = ((float)(x - timeAxisA))*timeAxisM + timeAxisB;
	  newXStart = xAxisValue;
	  inXAxisSelect = TRUE;
	} else {
	  savedActiveRx = activeRx;
	  strcpy(savedBslnFilter, bslnFilter);
	  strcpy(savedBlockFilter, blockFilter);
	  strcpy(savedChunkFilter, chunkFilter);
	  strcpy(savedSBFilter, sBFilter);
	  strcpy(savedSourceFilter, sourceFilter);
	  savedCurrentSource = currentSource;
	  for (i = 0; i < 11; i++)
	    for (j = 0; j < 11; j++) {
	      savedRequestedBaselines[i][j] = requestedBaselines[i][j];
	      requestedBaselines[i][j] = 0;
	    }
	  savedNBlocksRequested = nBlocksRequested;
	  for (i = 0; i < N_IFS*N_BLOCKS + 1; i++) {
	    savedRequestedBlockList[i] = requestedBlockList[i];
	    requestedBlockList[i] = 0;
	  }
	  savedNChunks = nChunks;
	  for (i = 0; i < N_CHUNKS; i++) {
	    savedChunkList[i] = chunkList[i];
	    chunkList[i] = 0;
	  }
	  savedNSidebands = nSidebands;
	  for (i = 0; i < 2; i++) {
	    savedSBList[i] = sBList[i];
	    sBList[i] = 0;
	  }
	  requestedBaselines[nextCell->ant1][nextCell->ant2] =
	    requestedBaselines[nextCell->ant2][nextCell->ant1] = 1;
	  nBlocksRequested = 1;
	  requestedBlockList[nextCell->block] = 1;
	  nChunks = 1;
	  activeRx = nextCell->iEf;
	  chunkList[0] = nextCell->chunk;
	  nSidebands = 1;
	  sBList[0] = nextCell->sb;
	  sprintf(bslnFilter, "%d-%d", nextCell->ant1, nextCell->ant2);
	  if (!doubleBandwidth || (activeRx == LOW_RX_CODE))
	    sprintf(blockFilter, "%d", (nextCell->block)+1);
	  else
	    sprintf(blockFilter, "%d", 12-(nextCell->block));
	  sprintf(chunkFilter, "%d", (nextCell->chunk)+1);
	  if (nextCell->sb == 0)
	    sprintf(sBFilter, "L");
	  else
	    sprintf(sBFilter, "U");
	  leftMargin = ZOOMED_LEFT_MARGIN;
	  rightMargin = ZOOMED_RIGHT_MARGIN;
	  topMargin = ZOOMED_TOP_MARGIN;
	  bottomMargin = ZOOMED_BOTTOM_MARGIN;
	  havePlottedSomething = FALSE;
	  shouldSayRedrawing = TRUE;
	  sayRedrawing();
	  zoomed = TRUE;
	  if (nextCell->block == SWARM_BLOCK)
	    sWARMZoomed = TRUE;
	}
      } else {
	/*
	  The click wasn't in a cell - check if it was in a label
	*/
	lock_label("CB");
	nextLabel = labelBase;
	while ((nextLabel != NULL) && (!found)) {
	  if ((nextLabel->tlcx < x) && (nextLabel->tlcy < y) &&
	      (nextLabel->trcx > x) && (nextLabel->trcy < y) &&
	      (nextLabel->brcx > x) && (nextLabel->brcy > y) &&
	      (nextLabel->blcx < x) && (nextLabel->blcy > y)) {
	    havePlottedSomething = FALSE;
	    found = TRUE;
	  } else
	    nextLabel = (label *)nextLabel->next;
	}
	if (found) {
	  dprintf("Label found (%d, %d), (%d, %d), (%d, %d), (%d, %d) source = %d\n",
		  nextLabel->tlcx, nextLabel->tlcy,
		  nextLabel->trcx, nextLabel->trcy,
		  nextLabel->blcx, nextLabel->blcy,
		  nextLabel->brcx, nextLabel->brcy, nextLabel->source);
	  if (nextLabel->ant1 == 1000) {
	    resetFilters();
	  } else {
	    if (nextLabel->ant1 > 0) {
	      for (i = 0; i < 11; i++)
		for (j = 0; j < 11; j++)
		  requestedBaselines[i][j] = 0;
	      requestedBaselines[nextLabel->ant1][nextLabel->ant2] =
		requestedBaselines[nextLabel->ant2][nextLabel->ant1] = 1;
	      sprintf(bslnFilter, "%d-%d", nextLabel->ant1, nextLabel->ant2);
	    }
	    if (nextLabel->source >= 0) {
	      if (button != 3)
		if (currentSource != nextLabel->source)
		  currentSource = nextLabel->source;
		else
		  currentSource = -1;
	      else
		blackListedSource[nextLabel->source] = ~blackListedSource[nextLabel->source];
	    }
	    if (nextLabel->block >= 0) {
	      for (i = 0; i < N_IFS*N_BLOCKS + 1; i++)
		requestedBlockList[i] = 0;
	      nBlocksRequested = 1;
	      plotOneBlockOnly = TRUE;
	      requestedBlockList[nextLabel->block] = 1;
	      sprintf(blockFilter, "%d", (nextLabel->block)+1);
	    }
	    if (nextLabel->chunk >= 0) {
	      nChunks = 1;
	      chunkList[0] = nextLabel->chunk;
	      sprintf(chunkFilter, "%d", (nextLabel->chunk)+1);
	    }
	    if (nextLabel->sb >= 0) {
	      nSidebands = 1;
	      sBList[0] = nextLabel->sb;
	      if (nextLabel->sb == 0)
		sprintf(sBFilter, "L");
	      else
		sprintf(sBFilter, "U");
	    }
	    if ((nextLabel->iEf != DONT_CHANGE_RX) && (nextLabel->source < 0))
	      activeRx = nextLabel->iEf;
	  }
	}
	unlock_label("CB");
      }
      unlock_cell("CB");
    }
    if (!inXAxisSelect) {
      shouldSayRedrawing = TRUE;
      forceRedraw("inputCB");
    }
  } else if (ptr->event->type == KeyPress) {
    int i, j, ant;
    char string[10];
    KeySym whatsThis;
    static int sawEscape = FALSE;

    XLookupString(&(*Xptr).xkey, string, 9, &whatsThis, NULL);
    if (helpScreenActive) {
      helpScreenActive = FALSE;
      refreshDisplay();
    } else {
      if (debugMessagesOn)
	printf("\"%c\" key seen \n", string[0]);
      switch ((int)string[0]) {
      case ESCAPE:
	sawEscape = TRUE;
	break;
      case 'b':
      case 'B':
	if (!helpScreenActive) {
	  XClearWindow(myDisplay, myWindow);
	  XFlush(myDisplay);
	  helpScreenActive = TRUE;
	}
	break;
      case 'h':
      case 'H':
	if (!helpScreenActive) {
	  printHelp();
	  helpScreenActive = TRUE;
	}
	break;
      case 'f':
      case 'F':
	if (disableUpdates) {
	  XCopyGC(myDisplay, whiteGc, 0x7fffff, labelGc);
	  XmToggleButtonSetState(freezeToggle, FALSE, FALSE);
	  disableUpdates = FALSE;
	} else {
	  XCopyGC(myDisplay, blueGc, 0x7fffff, labelGc);
	  XmToggleButtonSetState(freezeToggle, TRUE, FALSE);
	  disableUpdates = TRUE;
	}
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
	break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
	if (showClosure) {
	  closureBaseAnt = (int)(string[0] - '0');
	  if (closureBaseAnt < 1)
	    closureBaseAnt = 10;
	} else {
	  ant = (int)string[0] - (int)'0';
	  if (sawEscape) {
	    char addString[7];

	    bslnFilter[0] = (char)0;
	    for (i = 0; i < 11; i++)
	      for (j = 0; j < 11; j++)
		if ((i == ant) || (j == ant))
		  requestedBaselines[i][j] = FALSE;
	    for (i = 1; i < 8; i++)
	      for (j = i+1; j <= 8; j++)
		if (requestedBaselines[i][j] && antennaInArray[i] && antennaInArray[j]) {
		  sprintf(addString, "%d-%d ", i, j);
		  strcat(bslnFilter, addString);
		}
	    if (strlen(bslnFilter) > 2)
	      bslnFilter[strlen(bslnFilter)-1] = (char)0;
	  } else {
	    if ((ant > 0) && (ant < 10))
	      sprintf(bslnFilter, "%d-*", ant);
	    else
	      sprintf(bslnFilter, "*-*");
	    for (i = 0; i < 11; i++)
	      for (j = 0; j < 11; j++)
		if ((i == ant) || (j == ant) || (string[0] == '0'))
		  requestedBaselines[i][j] = TRUE;
		else
		  requestedBaselines[i][j] = FALSE;
	  }
	  shouldSayRedrawing = TRUE;
	  if (scanMode)
	    redrawScreen();
	  else
	    redrawScreenTrack();
	  break;
	}
      case '.':
	userSelectedPointSize = 0;
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case '-':
	userSelectedPointSize--;
	if (userSelectedPointSize < 2)
	  userSelectedPointSize = 0;
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case '(':
	if (!scanMode) {
	  gcArrayOffset += 1;
	  shouldSayRedrawing = TRUE;
	  redrawScreenTrack();
	}
	break;
      case ')':
	if (!scanMode) {
	  gcArrayOffset -= 1;
	  shouldSayRedrawing = TRUE;
	  redrawScreenTrack();
	}
	break;
      case '^':
	if (bslnOrder) {
	  bslnOrder = FALSE;
	  gotBslnLength = FALSE;
	  XmToggleButtonSetState(bslnOrderToggle, FALSE, FALSE);
	} else {
	  bslnOrder = TRUE;
	  XmToggleButtonSetState(bslnOrderToggle, TRUE, FALSE);
	}
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case '+':
	if (userSelectedPointSize == 0)
	  userSelectedPointSize = 2;
	userSelectedPointSize++;
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case '=':
	startScan = endScan = -1;
	startTime = endTime = -100.0;
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case '%':
	if (checkStatistics)
	  checkStatistics = FALSE;
	else
	  checkStatistics = TRUE;
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case '\t':
	if (scanMode) {
	  if (!haveTrackDirectory) {
	    selectCurrentTrack();
	    XmToggleButtonSetState(scanToggle, FALSE, FALSE);
	    startScan = endScan = -1;
	  }
	  scanMode  = FALSE;
	} else
	  scanMode = TRUE;
	havePlottedSomething = FALSE;
	setSensitivities();
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case '!':
	if (debugMessagesOn)
	  debugMessagesOn = FALSE;
	else
	  debugMessagesOn = TRUE;
	break;
      case '*':
	resetFilters();
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'z':
      case 'Z':
	if (plotFromZero)
	  plotFromZero = FALSE;
	else
	  plotFromZero = TRUE;
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'g':
      case 'G':
	if (autoscaleAmplitude)
	  autoscaleAmplitude = FALSE;
	else
	  autoscaleAmplitude = TRUE;
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'i':
      case 'I':
	if (!integrate) {
	  FILE *projectFile;
	  
	  projectFile = fopen("/sma/rtdata/engineering/monitorLogs/littleLog.txt", "r");
	  if (projectFile != NULL) {
	    fscanf(projectFile, "%s", &integrateSource[0]);
	    fclose(projectFile);
	  } else
	    integrateSource[0] = (char)0;
	  integrate = TRUE;
	} else
	  integrate = FALSE;
	break;
      case 't':
      case 'T':
	if (showRefresh) {
	  XmToggleButtonSetState(showRefreshToggle, FALSE, FALSE);
	  showRefresh = FALSE;
	} else {
	  XmToggleButtonSetState(showRefreshToggle, TRUE, FALSE);
	  showRefresh = TRUE;
	}
	break;
      case '@':
	autoCorrMode = !autoCorrMode;
	if (autoCorrMode)
	  saveRestoreFilters(TRUE);
	else
	  saveRestoreFilters(FALSE);
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'A':
	autoscaleAmplitude = !autoscaleAmplitude;
	if (autoscaleAmplitude)
	  XmToggleButtonSetState(autoscaleToggle, TRUE, FALSE);
	else
	  XmToggleButtonSetState(autoscaleToggle, FALSE, FALSE);
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'P':
	autoscalePhase = !autoscalePhase;
	if (autoscalePhase)
	  XmToggleButtonSetState(autoscalePhaseToggle, TRUE, FALSE);
	else
	  XmToggleButtonSetState(autoscalePhaseToggle, FALSE, FALSE);
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'S':
	shouldPlotSWARM = !shouldPlotSWARM;
	if (shouldPlotSWARM)
	  XmToggleButtonSetState(sWARMToggle, TRUE, FALSE);
	else
	  XmToggleButtonSetState(sWARMToggle, FALSE, FALSE);
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'a':
	if (!showAmp) {
	  showAmp = TRUE;
	  showLags = FALSE;
	  showClosure = FALSE;
	  XmToggleButtonSetState(ampToggle, TRUE, FALSE);
	  XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	  XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	} else {
	  XmToggleButtonSetState(ampToggle, FALSE, FALSE);
	  showAmp = FALSE;
	}
	shouldSayRedrawing = TRUE;
	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'c':
      case 'C':
	if (!scanMode) {
	  if (!showClosure) {
	    showClosure = TRUE;
	    showPhase = TRUE;
	    XmToggleButtonSetState(closureToggle, TRUE, FALSE);
	    XmToggleButtonSetState(phaseToggle, FALSE, FALSE);
	    if (showAmp) {
	      showLags = FALSE;
	      XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	      XmToggleButtonSetState(ampToggle, FALSE, FALSE);
	      showAmp = FALSE;
	    }
	    if (showCoh) {
	      XmToggleButtonSetState(cohToggle, FALSE, FALSE);
	      showCoh = FALSE;
	    }
	  } else {
	    showClosure = FALSE;
	    XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	    XmToggleButtonSetState(phaseToggle, TRUE, FALSE);
	    XmToggleButtonSetState(ampToggle, TRUE, FALSE);
	    showAmp = showPhase = TRUE;
	  }
	  shouldSayRedrawing = TRUE;
	  redrawScreenTrack();
	}
	break;
      case 'r':
      case 'R':
	if (activeRx == HIGH_RX_CODE) {
	  activeRx = LOW_RX_CODE;
	  if (doubleBandwidth)
	    sprintf(rxFilter, "Lower 2 GHz ");
	  else
	    sprintf(rxFilter, "Low Freq. Rx");
	  XmToggleButtonSetState(hiToggle, FALSE, FALSE);
	} else {
	  activeRx = HIGH_RX_CODE;
	  if (doubleBandwidth)
	    sprintf(rxFilter, "Upper 2 GHz");
	  else
	    sprintf(rxFilter, "High Freq. Rx");
	  XmToggleButtonSetState(loToggle, FALSE, FALSE);
	}
	shouldSayRedrawing = TRUE;
       	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'p':
	if (!showPhase) {
	  showLags = FALSE;
	  XmToggleButtonSetState(phaseToggle, TRUE, FALSE);
	  XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	  showClosure = FALSE;
	  XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	  showPhase = TRUE;
	} else {
	  XmToggleButtonSetState(phaseToggle, FALSE, FALSE);
	  showPhase = FALSE;
	}
	shouldSayRedrawing = TRUE;
      	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'M':
      case 'm':
	selectCurrentTrack();
	scanMode = FALSE;
	havePlottedSomething = FALSE;
	setSensitivities();
	XmToggleButtonSetState(scanToggle, FALSE, FALSE);
	startScan = endScan = -1;
	shouldSayRedrawing = TRUE;
      	redrawScreenTrack();
      break;
      case 'O':
      case 'o':
	sprintf(sourceFilter, "*");
	currentSource = -1;
	selectedSourceType = -1;
	bzero(blackListedSource, 500*sizeof(int));
	shouldSayRedrawing = TRUE;
      	redrawScreenTrack();
	break;
      case 'k':
      case 'K':
	if (!scanMode) {
	  if (showCoh) {
	    showCoh = FALSE;
	    showAmp = TRUE;
	    XmToggleButtonSetState(ampToggle, TRUE, FALSE);
	    XmToggleButtonSetState(closureToggle, FALSE, FALSE);
	  } else {
	    showCoh = TRUE;
	    showClosure = FALSE;
	    XmToggleButtonSetState(closureToggle, TRUE, FALSE);
	    if (showAmp) {
	      showLags = FALSE;
	      XmToggleButtonSetState(lagsToggle, FALSE, FALSE);
	      XmToggleButtonSetState(ampToggle, FALSE, FALSE);
	      showAmp = FALSE;
	    }
	  }
	  shouldSayRedrawing = TRUE;
	  redrawScreenTrack();
	}
	break;
      case 'l':
      case 'L':
	nSidebands = 1;
	sBList[0] = 0;
	sBFilter[0] = 'L';
	shouldSayRedrawing = TRUE;
       	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'u':
      case 'U':
	nSidebands = 1;
	sBList[0] = 1;
	sBFilter[0] = 'U';
	shouldSayRedrawing = TRUE;
      	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'D':
	debugMessagesOn = !debugMessagesOn;
	break;
      case 'd':
	nSidebands = 2;
	sBList[0] = 0;
	sBList[1] = 1;
	sBFilter[0] = '*';
	shouldSayRedrawing = TRUE;
       	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'e':
      case 'E':
	unmarkCallback();
	shouldSayRedrawing = TRUE;
       	if (scanMode)
	  redrawScreen();
	else
	  redrawScreenTrack();
	break;
      case 'q':
	exit(0);
      case 's':
	scanMode  = TRUE;
	havePlottedSomething = FALSE;
	setSensitivities();
	shouldSayRedrawing = TRUE;
       	redrawScreen();
	break;
      default:
	if (debugMessagesOn)
	  printf("I'm ignoring the \"%s\" key.\n", string);
      }
      if ((int)string[0] != ESCAPE)
	sawEscape = FALSE;
    }
  }
}

void buttonPress(w, popup, event)
Widget w;
Widget popup;
XButtonEvent * event;
{
  printf("I'm in butonPress!\n");
}

int compareFreq(const void *one, const void *two)
{
  if (((lineFreqKey *)one)->ptr->frequency < ((lineFreqKey *)two)->ptr->frequency)
    return(-1);
  else if (((lineFreqKey *)one)->ptr->frequency == ((lineFreqKey *)two)->ptr->frequency)
    return(0);
  else
    return(1);
}

void readLineCatalog(void) {
  int i, j;
  int parsedCorrectly = TRUE;
  char inLine[1024];
  lineEntry *ptr;
  FILE *catalog;

  catalog = fopen("/global/catalogs/sma_line_catalog_new", "r");
  if (catalog == NULL) {
    perror("open of line catalog");
    return;
  }
  while ((!feof(catalog)) && parsedCorrectly) {
    fgets(inLine, 1024, catalog);
    if (!feof(catalog))
      if (addLineEntry(inLine) != SUCCESS_RETURN)
	parsedCorrectly = FALSE;
  }
  if (!parsedCorrectly)
    return;
  /*
  printf("Read in info for %d lines\n", nLineEntries);
  */
  fullFreqIndex = (lineFreqKey *)malloc(nLineEntries*sizeof(lineFreqKey));
  if (fullFreqIndex == NULL) {
    perror("malloc for fullFreqIndex");
    return;
  }
  ptr = lineRoot;
  for (i = 0; i < nLineEntries; i++) {
    fullFreqIndex[i].ptr = ptr;
    ptr = ptr->next;
  }
  qsort(fullFreqIndex, nLineEntries, sizeof(lineFreqKey), compareFreq);
  /*
  for (i = 0; i < nLineEntries; i++)
    printf("Line %d:\t%f\t\"%s\"\n", i, fullFreqIndex[i].ptr->frequency, fullFreqIndex[i].ptr->nickName);
  */
  shortFreqIndex = (lineFreqKey *)malloc(nNamedLines*sizeof(lineFreqKey));
  if (shortFreqIndex == NULL) {
    perror("malloc for shortFreqIndex");
    return;
  }
  ptr = lineRoot;
  j = 0;
  for (i = 0; i < nLineEntries; i++) {
    if (ptr->hasNickName)
      shortFreqIndex[j++].ptr = ptr;
    ptr = ptr->next;
  }
  qsort(shortFreqIndex, nNamedLines, sizeof(lineFreqKey), compareFreq);
  for (i = 0; i < nNamedLines; i++)
    dprintf("Line %d:\t%f\t\"%s\"\n", i, shortFreqIndex[i].ptr->frequency, shortFreqIndex[i].ptr->nickName);
  gotLineInfo = TRUE;
}

int main(int argc, char** argv)
{
  int i, j, rc;
  int mirmode = FALSE;
  int debug = FALSE;
  int help = FALSE;
  int lowerFlag = FALSE;
  int upperFlag = FALSE;
  int usage = FALSE;
  char *filename = "none";
  char *receiver = "default";
  struct poptOption optionsTable[] = {
    {"debug", 'd', POPT_ARG_NONE, &debug, 0, "Print lots of debugging info"},
    {"file", 'f', POPT_ARG_STRING, &filename, 0, "Optional file name for mir-mode"},
    {"lower", 'l', POPT_ARG_NONE, &lowerFlag, 0, "Start up showing LSB only"},
    {"upper", 'u', POPT_ARG_NONE, &upperFlag, 0, "Start up showing USB only"},
    {"receiver", 'R', POPT_ARG_STRING, &receiver, 0, "Receiver (\"l\" or \"h\", default lower)"},
    {"mir_mode", 'm', POPT_ARG_NONE, &mirmode, 0, "Initialize the program in mir-mode"},
    POPT_AUTOHELP
    {NULL,0,0,NULL,0,0},
    {"\ncorrPlotter displays the output of the SMA correlator.   It has two modes.\n\"scan-mode\" displays each scan as a function of frequency,\nas soon as the correlator has completed the scan.   \"mir-mode\" displays\n the pseudocontinuum amplitude and phase as a function of time."
    }
  };
  static poptContext optCon;

  Arg		args[20];
  Cardinal	n;
  GC		gc;
  FILE *corrStats;

  checkForDoubleBandwidth();
  optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
  if ((rc = poptGetNextOpt(optCon)) < -1) {
    fprintf(stderr, "setChunkPhaseOffsets: bad argument %s: %s\n",
            poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
            poptStrerror(rc));
    return 2;
  }
                                                                                
  if (help) {
    poptPrintHelp(optCon, stdout, 0);
    return 0;
  } if (usage) {
    poptPrintUsage(optCon, stdout, 0);
    return 0;
  }
  getAntennaList(antennaInArray);
  if (upperFlag && lowerFlag) {
    fprintf(stderr, "You cannot specify both -u and -l\n");
    exit(-1);
  }
  if (!mirmode) {
    readLineCatalog();
    corrStats = fopen("/global/configFiles/corrStatThreshold", "r");
    if (corrStats == NULL) {
      perror("/global/configFiles/corrStatThreshold");
      fprintf(stderr, "Error opening sample statistics threshold file - will use default thresholds\n");
    } else {
      fscanf(corrStats, "%f %f %f %f", &badNLevelLow, &badNLevelHigh, &bad1LevelLow, &bad1LevelHigh);
      badNLevelLow *= 0.01;
      badNLevelHigh *= 0.01;
      bad1LevelLow *= 0.01;
      bad1LevelHigh *= 0.01;
      fclose(corrStats);
    }
  }
  if (strcmp(filename, "none") == 0)
    sprintf(trackDirectory, "Your Ad. Here");
  else {
    haveTrackDirectory = TRUE;
    strcpy(trackDirectory, filename);
  }
  sprintf(bslnFilter, "*-*");
  sprintf(sourceFilter, "*");
  sprintf(blockFilter, "*");
  sprintf(chunkFilter, "*");
  sprintf(sBFilter, "*");
  activeRx = LOW_RX_CODE;
  if (doubleBandwidth)
    sprintf(rxFilter,"Lower 2 GHz ");
  else
    sprintf(rxFilter,"Low Freq. Rx");
  for (i = 0; i < 11; i++)
    for (j = 0; j < 11; j++)
      requestedBaselines[i][j] = 1;

  bzero(&correlator, sizeof(correlatorDef));

  if (pthread_mutex_init(&xDisplayMut,
			 &xDisplayMutAttr) == SYSTEM_FAILURE) {
    perror("X11 Mutex create");
    exit(SYSTEM_FAILURE);
  }
  if (pthread_mutex_init(&dataMut,
			 &xDisplayMutAttr) == SYSTEM_FAILURE) {
    perror("Data Mutex create");
    exit(SYSTEM_FAILURE);
  }
  if (pthread_mutex_init(&labelMut,
			 &xDisplayMutAttr) == SYSTEM_FAILURE) {
    perror("Label Mutex create");
    exit(SYSTEM_FAILURE);
  }
  if (pthread_mutex_init(&cellMut,
			 &xDisplayMutAttr) == SYSTEM_FAILURE) {
    perror("cell Mutex create");
    exit(SYSTEM_FAILURE);
  }
  if (pthread_mutex_init(&mallocMut,
			 &xDisplayMutAttr) == SYSTEM_FAILURE) {
    perror("malloc Mutex create");
    exit(SYSTEM_FAILURE);
  }
  if (pthread_mutex_init(&trackMut,
			 &xDisplayMutAttr) == SYSTEM_FAILURE) {
    perror("track Mutex create");
    exit(SYSTEM_FAILURE);
  }
  pthread_attr_setinheritsched(&sleeperAttr, PTHREAD_INHERIT_SCHED);
  if (pthread_create(&sleeperTId, NULL, sleeper, (void *) 12) ==
      SYSTEM_FAILURE) {
    perror("pthread_create (sleeper)");
    exit(SYSTEM_FAILURE);
  }
  if (pthread_create(&timerTId, NULL, timer, (void *) 12) ==
      SYSTEM_FAILURE) {
    perror("pthread_create (timer)");
    exit(SYSTEM_FAILURE);
  }
  n = 0;
  if (!XtToolkitThreadInitialize()) {
    printf("Nuts - threads not supported\n");
    exit(-1);
  }
  XtSetArg(args[n], XmNmappedWhenManaged, FALSE); n++;
  if (debugMessagesOn)
    printf("Calling XtAppInitialize\n");
  rootParent = XtAppInitialize(&app_context,
			       "XCorrPlotter",
			       (XrmOptionDescList) NULL,
			       0,
			       (int *) &argc,
			       argv,
			       (String *) NULL,
			       args, n);
  lock_X_display();
  if (doubleBandwidth)
    displayWidth = (int)((float)displayWidth * 1.5);
  displayWidth+= N_SWARM_CHUNKS*200;
  
  /* Create Main Window */
  
  n = 0;
  XtSetArg(args[n], XmNheight , 550); n++;
  XtSetArg(args[n], XmNwidth , displayWidth); n++;
  XtSetArg(args[n], XmNbackground , black.pixel); n++;
  mainwindow = XmCreateMainWindow(rootParent, "main", args, n);

/* Create Menubar */
  
  n = 0;
  menubar = XmCreateMenuBar(mainwindow, "menubar", args, n);
  XtManageChild(menubar);
  
  /* Create File, Filter, Options, Mode  and Help menus */
  
  filemenu = CreatePulldownMenu(menubar, "File", TRUE);
  rxmenu = CreatePulldownMenu(menubar, "Receiver", TRUE);
  filtermenu = CreatePulldownMenu(menubar, "Filter", TRUE);
  optionsmenu = CreatePulldownMenu(menubar, "Options", TRUE);
  modemenu = CreatePulldownMenu(menubar, "Mode", TRUE);
  helpmenu = CreateHelpMenu(menubar, "Help");
  
  /* Set up menu choices for FILE menu */
  currentTrackButton = CreatePushbutton(filemenu, "Display Current Track", (XtCallbackProc) currentTrackCB,
    (XtPointer) "currentTrack");
  trackButton = CreatePushbutton(filemenu, "Display Track from List", (XtCallbackProc) trackCB,
    (XtPointer) "track");
  (void) CreatePushbutton(filemenu, "exit", (XtCallbackProc) exitCB,
			  (XtPointer) "Exit");

  /* Set up menu choices for Receiver menu */

  hiToggle = CreateToggleButton(rxmenu, "High Frequency", (XtCallbackProc) toggleCB,
    (XtPointer) "hi");
  if (activeRx == HIGH_RX_CODE)
    XmToggleButtonSetState(hiToggle, TRUE, FALSE);
  loToggle = CreateToggleButton(rxmenu, "Low Frequency", (XtCallbackProc) toggleCB,
    (XtPointer) "lo");
  if (activeRx == LOW_RX_CODE)
    XmToggleButtonSetState(loToggle, TRUE, FALSE);
 
  /* Set up menu choices for filter menu */
  /*
  rxButton = CreatePushbutton(filtermenu, "Select Receiver", (XtCallbackProc) rxCB,
    (XtPointer) "rx");
  */
   bslnButton = CreatePushbutton(filtermenu, "Select Baseline(s)", (XtCallbackProc) bslnCB,
    (XtPointer) "bsln");
  blockButton = CreatePushbutton(filtermenu, "Select Block(s)", (XtCallbackProc) blockCB,
    (XtPointer) "block");
  chunkButton = CreatePushbutton(filtermenu, "Select Chunk(s)", (XtCallbackProc) chunkCB,
    (XtPointer) "chunk");
  sBButton = CreatePushbutton(filtermenu, "Select Sideband(s)", (XtCallbackProc) sBCB,
    (XtPointer) "sb");
  chanButton = CreatePushbutton(filtermenu, "Select Channel Range", (XtCallbackProc) chanCB,
    (XtPointer) "chan");
  rangeButton = CreatePushbutton(filtermenu, "Select Scan Range", (XtCallbackProc) rangeCB,
    (XtPointer) "range");
  timeRangeButton = CreatePushbutton(filtermenu, "Select Time Range", (XtCallbackProc) timeRangeCB,
    (XtPointer) "timeRange");
  /*
  testButton = CreatePushbutton(filtermenu, "Test", (XtCallbackProc) testCB,
    (XtPointer) "range");
  */
  /*
  sourceButton = CreatePushbutton(filtermenu, "Select Source", (XtCallbackProc) sourceCB,
    (XtPointer) "source");
  */
  typeButton = CreatePushbutton(filtermenu, "Select Source Type", (XtCallbackProc) typeCB,
    (XtPointer) "rf");
  polButton = CreatePushbutton(filtermenu, "Select Pol. State", (XtCallbackProc) polTypeCB,
    (XtPointer) "rf");
  sBButton = CreatePushbutton(filtermenu, "Reset All Filters", (XtCallbackProc) rFCB,
    (XtPointer) "rf");

  /* Set up choices for the "Mode" menu */
  scanToggle = CreateToggleButton(modemenu, "Scan mode", toggleCB, "scan mode");
  if (mirmode || (!corrSaverMachine))
    scanMode = FALSE;
  else
    scanMode = TRUE;
  if (debug)
    debugMessagesOn = TRUE;
  if (scanMode)
    XmToggleButtonSetState(scanToggle, TRUE, FALSE);
  else if (!haveTrackDirectory)
    selectCurrentTrack();

  /* Set up choices under the "Help" menu */
  helpButton = CreatePushbutton(helpmenu, "Contents", (XtCallbackProc) helpCB,
    (XtPointer) "help");

  /* Set up choices for "Options" menu */
  
  statsToggle = CreateToggleButton(optionsmenu, "Check Sample Statistics", toggleCB, "checkStatistics");
  if (checkStatistics)
    XmToggleButtonSetState(statsToggle, TRUE, FALSE);
  ampToggle = CreateToggleButton(optionsmenu, "Plot Amplitude", toggleCB, "showAmp");
  if (showAmp)
    XmToggleButtonSetState(ampToggle, TRUE, FALSE);
  phaseToggle = CreateToggleButton(optionsmenu, "Plot Phase", toggleCB, "showPhase");
  if (showPhase)
    XmToggleButtonSetState(phaseToggle, TRUE, FALSE);
  cohToggle = CreateToggleButton(optionsmenu, "Plot Coherence", toggleCB, "showCoh");
  if (showCoh)
    XmToggleButtonSetState(cohToggle, TRUE, FALSE);
  closureToggle = CreateToggleButton(optionsmenu, "Plot Closure Phase", toggleCB, "closure");
  if (showClosure)
    XmToggleButtonSetState(closureToggle, TRUE, FALSE);
  lagsToggle = CreateToggleButton(optionsmenu, "Plot Them Lags", toggleCB, "showLags");
  if (showLags)
    XmToggleButtonSetState(lagsToggle, TRUE, FALSE);
  autoscaleToggle = CreateToggleButton(optionsmenu, "Autoscale Amp.", toggleCB, "autoscaleAmplitude");
  if (autoscaleAmplitude)
    XmToggleButtonSetState(autoscaleToggle, TRUE, FALSE);
  autoscalePhaseToggle = CreateToggleButton(optionsmenu, "Autoscale Phase.", toggleCB, "autoscalePhase");
  if (autoscalePhase)
    XmToggleButtonSetState(autoscalePhaseToggle, TRUE, FALSE);
  sWARMLogToggle = CreateToggleButton(optionsmenu, "Log plot SWARM Autos", toggleCB, "logPlotAutos");
  if (sWARMLogPlot)
    XmToggleButtonSetState(sWARMLogToggle, TRUE, FALSE);
  sWARMLineToggle = CreateToggleButton(optionsmenu, "Line plot SWARM Autos", toggleCB, "linePlotAutos");
  if (sWARMLinePlot)
    XmToggleButtonSetState(sWARMLineToggle, TRUE, FALSE);
  sWARMToggle = CreateToggleButton(optionsmenu, "Plot SWARM Data", toggleCB, "plotSWARM");
  if (shouldPlotSWARM)
    XmToggleButtonSetState(sWARMToggle, TRUE, FALSE);
  bandLabelingToggle = CreateToggleButton(optionsmenu, "Label Chunks by Band Name", toggleCB, "bandLabeling");
  if (bandLabeling)
    XmToggleButtonSetState(bandLabelingToggle, TRUE, FALSE);
  plotFromZeroToggle = CreateToggleButton(optionsmenu,
					"Plot Amp or Coh From Zero", toggleCB, "plotFromZero");
  if (plotFromZero)
    XmToggleButtonSetState(plotFromZeroToggle, TRUE, FALSE);
  plotToOneToggle = CreateToggleButton(optionsmenu,
				       "Plot Coherence to One", toggleCB, "plotToOne");
  if (plotToOne)
    XmToggleButtonSetState(plotToOneToggle, TRUE, FALSE);
  logPlotToggle = CreateToggleButton(optionsmenu,
				     "Log Plot Amp or Coh", toggleCB, "logPlot");
  if (logPlot)
    XmToggleButtonSetState(logPlotToggle, TRUE, FALSE);
  scaleInMHzToggle = CreateToggleButton(optionsmenu,
					"X Scale in MHz", toggleCB, "scaleInMHz");
  if (scaleInMHz)
    XmToggleButtonSetState(scaleInMHzToggle, TRUE, FALSE);
  integrateToggle = CreateToggleButton(optionsmenu, "Integrate", toggleCB, "integrate");
  if (integrate)
    XmToggleButtonSetState(integrateToggle, TRUE, FALSE);
  showGoodToggle = CreateToggleButton(optionsmenu, "Show Good Points", toggleCB, "showGood");
  if (showGood)
    XmToggleButtonSetState(showGoodToggle, TRUE, FALSE);
  showBadToggle = CreateToggleButton(optionsmenu, "Show Bad Points", toggleCB, "showBad");
  if (showBad)
    XmToggleButtonSetState(showBadToggle, TRUE, FALSE);
  showRefreshToggle = CreateToggleButton(optionsmenu, "Show Refreshes", toggleCB, "showRefresh");
  if (showRefresh)
    XmToggleButtonSetState(showRefreshToggle, TRUE, FALSE);
  freezeToggle = CreateToggleButton(optionsmenu, "Disable Updates", toggleCB, "freeze");
  if (disableUpdates)
    XmToggleButtonSetState(freezeToggle, FALSE, FALSE);
  bslnOrderToggle = CreateToggleButton(optionsmenu, "Sort by Baseline Length",
				       toggleCB, "bslnOrder");
  if (bslnOrder)
    XmToggleButtonSetState(bslnOrderToggle, TRUE, FALSE);
  wrapColorToggle = CreateToggleButton(optionsmenu, "Wrap Source Colors",
				       toggleCB, "wrapColor");
  if (wrapColor)
    XmToggleButtonSetState(wrapColorToggle, TRUE, FALSE);
  timePlotToggle = CreateToggleButton(optionsmenu, "X Scale Time", toggleCB, "timePlot");
  if (timePlot)
    XmToggleButtonSetState(timePlotToggle, TRUE, FALSE);
  hAPlotToggle = CreateToggleButton(optionsmenu, "X Scale HA", toggleCB, "hAPlot");
  if (hAPlot)
    XmToggleButtonSetState(hAPlotToggle, TRUE, FALSE);
  applySecantZToggle = CreateToggleButton(optionsmenu, "Scale amp by sec(ZA)", toggleCB, "applySecantZ");
  if (applySecantZ)
    XmToggleButtonSetState(applySecantZToggle, TRUE, FALSE);
  gridToggle = CreateToggleButton(optionsmenu, "Show grid", toggleCB, "grid");
  if (grid)
    XmToggleButtonSetState(gridToggle, TRUE, FALSE);
  pointButton = CreatePushbutton(optionsmenu, "Change Point Size", (XtCallbackProc) pointCB,
    (XtPointer) "point");
  /*
  sWARMAveButton = CreatePushbutton(optionsmenu, "SWARM Points to Average", (XtCallbackProc) sWARMAveCB,
    (XtPointer) "sWARMAve");
  */
  closureBaseAntButton = CreatePushbutton(optionsmenu, "Change Closure Base Antenna", (XtCallbackProc) closureBaseAntCB,
    (XtPointer) "closureBaseAnt");
  pauseButton = CreatePushbutton(optionsmenu, "Set Interscan Pause", (XtCallbackProc) pauseCB,
    (XtPointer) "pause");
  useFullCatalogToggle = CreateToggleButton(optionsmenu,
					"Use full line catalog", toggleCB, "useFullCatalog");
  if (useFullCatalog)
    XmToggleButtonSetState(useFullCatalogToggle, TRUE, FALSE);
  debugToggle = CreateToggleButton(optionsmenu, "Print Debugging Messages", toggleCB, "debugMessages");
  if (debugMessagesOn)
    XmToggleButtonSetState(debugToggle, TRUE, FALSE);

  /* Create a "Drawing Area" to put the actual plot into */
  n = 0;
  
  XtSetArg(args[n], XmNbackground , 1); n++;
  XtSetArg(args[n], XmNresizePolicy, XmRESIZE_ANY); n++;
  drawing = XmCreateDrawingArea(mainwindow, "drawing", args, n);
  XtAddEventHandler(mainwindow, ButtonPressMask, False, buttonPress, mainwindow);
  XtAddCallback(drawing, XmNresizeCallback, (XtCallbackProc) resizeCB,
		(XtPointer) &gc);
  XtAddCallback(drawing, XmNexposeCallback, (XtCallbackProc) drawCB,
		(XtPointer) &gc);
  XtAddCallback(drawing, XmNinputCallback, (XtCallbackProc) inputCB,
		(XtPointer) &gc);
  XtManageChild(drawing);

  setSensitivities();
  /* Set Widgets into the proper areas in the main window */
  XmMainWindowSetAreas(mainwindow, menubar, (Widget) NULL, (Widget) NULL,
		       (Widget) NULL, drawing);
  
  XtManageChild(mainwindow);
  
  myDisplay = XtDisplay(drawing);
  myscreen = DefaultScreen(myDisplay);

  /* Set up icon for corrPlotter */

  icon = XCreateBitmapFromData(myDisplay,
			       RootWindow(myDisplay,DefaultScreen(myDisplay)), (char *)corrPlotter_bitmap_bits,
			       corrPlotter_bitmap_width, corrPlotter_bitmap_height);
  if (icon != (Pixmap)None)
    XtVaSetValues(rootParent,XmNiconName, "corrPlotter",
	XmNiconPixmap, icon, NULL);

  /* Realize widgets and enter the main loop */
  XtRealizeWidget(rootParent);
  
  myWindow = XtWindow(drawing);
  /* Create a pixmap for background plotting */
  XDepth = XDefaultDepth(myDisplay, myscreen);
  pixmap = XCreatePixmap(myDisplay, myWindow, displayWidth, displayHeight, XDepth);

  initGcs();

  XtMapWidget(rootParent);
  if (fieldSize == 1) {
    charHeight = bigFontStruc->max_bounds.ascent +
      bigFontStruc->max_bounds.descent;
  } else {
    charHeight = smallFontStruc->max_bounds.ascent +
      smallFontStruc->max_bounds.descent;
  }
  charHeight -= 4;
  if (lowerFlag) {
    nSidebands = 1;
    sBList[0] = 0;
    sBFilter[0] = 'L';
  } else if (upperFlag) {
    nSidebands = 1;
    sBList[0] = 1;
    sBFilter[0] = 'U';
  }
  if (tolower(receiver[0]) == 'h') {
    activeRx = HIGH_RX_CODE;
    if (doubleBandwidth)
      sprintf(rxFilter, "Upper 2 GHz  ");
    else
      sprintf(rxFilter, "High Freq. Rx");
    XmToggleButtonSetState(loToggle, FALSE, FALSE);
  }
  unlock_X_display(FALSE);
  XtAppMainLoop(app_context);
  return(0); /* Never gets here */
}
