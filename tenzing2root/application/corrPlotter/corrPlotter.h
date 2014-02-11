#ifndef CORR_PLOTTER
#define CORR_PLOTTER

#define PLT_KEY_ID 12345680
#define N_CRATES 12
#define N_IFS 2
#define N_SIDEBANDS 2
#define RB_UPPER_FLAG 1
#define RB_LOWER_FLAG 0
#define N_CHUNKS 4
#define N_ANTENNAS 10
#define N_SAMPLER_LEVELS 4
#define N_ANTENNAS_PER_BASELINE 2
#define N_BASELINES_PER_CRATE 28
#define N_CHANNELS_MAX 4096
#define N_SWARM_CHANNELS 16384
#define N_SWARM_CHUNKS 2
#define N_POLARIZATIONS 4

typedef struct dataHeader {
  int crateActive[N_CRATES];
  int receiverActive[N_IFS];
  int scanNumber[N_CRATES];
  double UTCTime[N_CRATES];
  double intTime[N_CRATES];
} dataHeader;

typedef struct resDescriptor {
  int pointsPerChunk[N_IFS][N_CHUNKS];
  int baselineInUse[N_IFS][N_BASELINES_PER_CRATE];
} resDescriptor;

typedef struct baselineData {
  int antenna[N_ANTENNAS_PER_BASELINE];
  float counts[N_IFS][N_ANTENNAS_PER_BASELINE][N_CHUNKS][N_SAMPLER_LEVELS];
  float amp[N_IFS][N_SIDEBANDS][N_CHANNELS_MAX];
  float phase[N_IFS][N_SIDEBANDS][N_CHANNELS_MAX];
} baselineData;

typedef struct crateDef {
  resDescriptor description;
  baselineData data[N_BASELINES_PER_CRATE];
} crateDef;

typedef struct sWARMBaselineData {
  int haveCrossData;
  int ant[N_ANTENNAS_PER_BASELINE];
  float amp[N_SWARM_CHUNKS][N_SIDEBANDS][N_SWARM_CHANNELS];
  float phase[N_SWARM_CHUNKS][N_SIDEBANDS][N_SWARM_CHANNELS];
} sWARMBaselineData;

typedef struct sWARMAutocorrelationData {
  int haveAutoData;
  float amp[N_SWARM_CHUNKS][N_SWARM_CHANNELS];
} sWARMAutocorrelationData;

typedef struct correlatorDef {
  int updating;
  dataHeader header;
  crateDef crate[N_CRATES];
  int sWARMScan;
  sWARMBaselineData sWARMBaseline[N_BASELINES_PER_CRATE];
  sWARMAutocorrelationData sWARMAutocorrelation[N_ANTENNAS];
} correlatorDef;
#endif
