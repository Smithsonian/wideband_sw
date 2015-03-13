#if 0
/* Share with OBS */
#define DSM_ADC_HIST "SWARM_SAMPLER_HIST_V2_V256_L"
#define OBS_CLASS "OBS"
/* Share with monitor */
#define DSM_CAL_STRUCT "SWARM_SAMPLER_CALIBRATIONS_X"
#define DSM_ADC_LOADING "SWARM_LOADING_FACTOR_V2_F"
#define DSM_MONITOR_CLASS "SWARM_MONITOR"
/* Share with Hal */
#define DSM_CMD "SWARM_ADC_CMD_V3_L"
#define DSM_CMD_RTN "SWARM_ADC_CMD_RETURN_V3_L"
#define HAL "hal9000"
#endif

/* Contents of SWARM_SAMPLER_CALIBRATIONS_X structure */
#define A_OFFS "ACTUAL_OFFSETS_V2_V4_F"
#define A_GAINS "ACTUAL_GAINS_V2_V4_F"
#define A_PHASES "ACTUAL_PHASES_V2_V4_F"
#define A_INL "ACTUAL_INL_V17_V2_V4_F"
#define SV_OFFS "SAVED_OFFSETS_V2_V4_F"
#define SV_GAINS "SAVED_GAINS_V2_V4_F"
#define SV_PHASES "SAVED_PHASES_V2_V4_F"
#define SV_INL "SAVED_INL_V17_V2_V4_F"
#define DEL_OFFS "DELTA_OFFSETS_V2_V4_F"
#define DEL_GAINS "DELTA_GAINS_V2_V4_F"
#define DEL_PHASES "DELTA_PHASES_V2_V4_F"
#define DEL_INL "DELTA_INL_V17_V2_V4_F"
#define DEL_OVL_CNT "DELTA_OVL_CNT_V2_V4_L"
#define DEL_AVZ "DELTA_AVZ_V2_F"
#define DEL_AVAMP "DELTA_AVAMP_V2_F"

enum adc_cmds {TAKE_SNAPSHOT = 0, MEASURE_OG, SET_OGP, GET_OGP,
  UPDATE_OGP, CLEAR_OGP, START_ADC_MONITOR, STOP_ADC_MONITOR, LAST_CMD};

#define ERR_STRINGS \
"Success", \
"Unknown command",\
"unable to acquire raw mode state",\
"fpga not programmed",\
"Cannot get SPI register",\
"Error opening file",\
"Snapshot registers not set up correctly", \
"Snapshot timed out",\
"Taking snapshot failed",\
"Bad repeat, must be in [1,2000]",\
"Bad zdok, must be 0 or 1",\
"Could not create adc_monitor thread",\
"ADC Monitor thread not active",\
"dsm_structure_init()",\
"dsm_structure_get_element failed",\
"dsm_structure_set_element failed",\
"dsm_read failed",\
"dms_write failed",\
"Errors reading back ogp registers"

enum adc_rtn_codes {OK, UNK, RAW_MODE, FPGA_PGM, SPI_REG, FILE_OPEN, SNAP_REG,
                    SNAP_TO, SNAP_FAIL,BAD_RPT, BAD_ZDOK, MONITOR_THREAD,
		    MONITOR_NOT_JOINED,
		    STRUCT_INIT, STRUCT_GET_ELEMENT, STRUCT_SET_ELEMENT,
		    DSM_READ, DSM_WRITE, OGP_READBACK, LAST_RTN};
