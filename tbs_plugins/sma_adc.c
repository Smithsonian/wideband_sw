#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include <katcp.h>
#include <sys/mman.h>
#include <tcpborphserver3.h>
#include <dsm.h>
#include <pthread.h>
#include <plugin.h>
#include "sma_adc.h"

#define TRUE 1
#define FALSE 0
#define OK 1
#define FAIL 0

#define SCOPE_0_DATA "scope_snap0_bram"
#define SCOPE_0_CTRL "scope_snap0_ctrl"
#define SCOPE_0_STATUS "scope_snap0_status"
#define SCOPE_1_DATA "scope_snap1_bram"
#define SCOPE_1_CTRL "scope_snap1_ctrl"
#define SCOPE_1_STATUS "scope_snap1_status"

#define CHECK_READBACK 1
#define UPDATE_FROM_OGP_FILE 1

#define SCOPE_SRC_CTRL 0
#if SCOPE_SRC_CTRL
/* two control registers for snapshots */
#define SCOPE_CTRL "scope_ctrl"
#define SOURCE_CTRL "source_ctrl"
#endif /* SCOPE_SRC_CTRL */

/* pointers for controlling and accessing the snapshot */
static int *ctrl_p, *stat_p;
static signed char *snap_p;
#if SCOPE_SRC_CTRL
static int *scope_ctrl_p, *source_ctrl_p;
#endif /* SCOPE_SRC_CTRL */
pthread_mutex_t snap_mutex;
struct snapshot_args {
  struct katcp_dispatch *d;
  struct tbs_raw *tr;
};

/* the following are for programming the adc through the spi interface. */
#define ADC5G_CONTROLLER "adc5g_controller"
#define CONTROL_REG_ADDR 0x01
#define CHANSEL_REG_ADDR 0x0f
#define EXTOFFS_REG_ADDR 0x20
#define EXTGAIN_REG_ADDR 0x22
#define EXTPHAS_REG_ADDR 0x24
#define CALCTRL_REG_ADDR 0x10
#define FIRST_EXTINL_REG_ADDR 0x30
#define SPI_WRITE 0x80;

/* Default file names */
#define SNAP_NAME "/instance/adcTests/snap"
#define OGP_BASE_NAME "/instance/configFiles/ogp_if"
#if 0
#define OGP_NAME "/instance/ogp"
#define OGP_MEAS_NAME "/instance/ogp_meas"
#endif

uint32_t *spi_p;

/* The ogp array contains o, g and p of core A followed by the same for cores
 * B, C and D.
 * In this case, since phase can not be determined, each p will be 0.
 * Overlaod_cnt will contain a count of -128 and 127 codes for each core.
 */
typedef struct {
  float ogp[12];
  float avz;
  float avamp;
  int overload_cnt[4];
} og_rtn;

dsm_structure dsm_adc_cal;

/* Things for the adc monitor thread */

int run_adc_monitor = FALSE;
pthread_t adc_monitor_thread;

#define ADC_HIST "SWARM_SAMPLER_HIST_V2_V256_L"
#define ADC_LOADING "SWARM_LOADING_FACTOR_V2_F"

struct tbs_raw *get_mode_pointer(struct katcp_dispatch *d){
  struct tbs_raw *tr;

  /* Grab the mode pointer and check that the FPGA is programmed */
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "unable to acquire raw mode state");
    return NULL;
  }

  /* Make sure we're programmed */
  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return NULL;
  }
  return(tr);
}

int set_spi_pointer(
    struct katcp_dispatch *d, struct tbs_raw *tr, int zdok) {
  struct tbs_entry *spi_reg;

  spi_reg = find_data_avltree(tr->r_registers, ADC5G_CONTROLLER);
  if(spi_reg == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Cannot get SPI register");
    return 0;
  }
  spi_p = ((uint32_t *)(tr->r_map + spi_reg->e_pos_base)) + 1 + zdok;
  return 1;
}

/* the following spi subroutines assume that spi_p is set up.  It also
 * assumes that it is running on a big-endian (network byte order) machine
 * like the ppc on a roach2*/

/* return the 16 bit value which was read or -1 if an error */
int get_spi_register(int reg_addr) {
  unsigned char spi_val[4];

  /* request the register */
  spi_val[0] = 0;
  spi_val[1] = 0;
  spi_val[2] = (unsigned char)reg_addr;
  spi_val[3] = 1;
  *spi_p = *(uint32_t *)spi_val;
  /* delay so that the request can complete */
  usleep(1000);
  /* read the return */
  *(uint32_t *)spi_val = *spi_p;
  if(spi_val[2] == (unsigned char)reg_addr) {
    return *(uint16_t *)spi_val;
  } else {
    return(-1);
  }
}

void set_spi_register(int reg_addr, int reg_val) {
  unsigned char spi_val[4];

  *(uint16_t *)spi_val = (uint16_t)reg_val;
  spi_val[2] = (unsigned char)(reg_addr) | 0x80;
  spi_val[3] = 1;
  *spi_p = *(uint32_t *)spi_val;
  /* delay so that this spi command will finish before another can start */
  usleep(1000);
}

float get_offset_register(int chan) {
  int rtn;

  set_spi_register(CHANSEL_REG_ADDR, chan);
  rtn = get_spi_register(EXTOFFS_REG_ADDR);
  return((rtn - 0x80)*(100./255.));
}

int set_offset_register(int chan, float offset) {
  int reg_val;

  reg_val = (int)(((offset < 0)? -0.5: 0.5) + offset*(255./100.)) + 0x80;
  set_spi_register(CHANSEL_REG_ADDR, chan);
  set_spi_register(EXTOFFS_REG_ADDR, reg_val);
  set_spi_register(CALCTRL_REG_ADDR, 2<<2);
  return reg_val;
}

float get_gain_register(int chan) {
  int rtn;

  set_spi_register(CHANSEL_REG_ADDR, chan);
  rtn = get_spi_register(EXTGAIN_REG_ADDR);
  return((rtn - 0x80)*(36./255.));
}

int set_gain_register(int chan, float gain) {
  int reg_val;

  reg_val = (int)(((gain < 0)? -0.5: 0.5) + gain*(255./36.)) + 0x80;
  set_spi_register(CHANSEL_REG_ADDR, chan);
  set_spi_register(EXTGAIN_REG_ADDR, reg_val);
  set_spi_register(CALCTRL_REG_ADDR, 2<<4);
  return reg_val;
}

float get_phase_register(int chan) {
  int rtn;

  set_spi_register(CHANSEL_REG_ADDR, chan);
  rtn = get_spi_register(EXTPHAS_REG_ADDR);
  return((rtn - 0x80)*(28./255.));
}

int set_phase_register(int chan, float phase) {
  int reg_val;

  reg_val = (int)(((phase < 0)? -0.5: 0.5) + phase*(255./28.)) + 0x80;
  set_spi_register(CHANSEL_REG_ADDR, chan);
  set_spi_register(EXTPHAS_REG_ADDR, reg_val);
  set_spi_register(CALCTRL_REG_ADDR, 2<<6);
  return reg_val;
}

void get_ogp_registers(float *ogp) {
  int chan;

  for(chan = 1; chan < 5; chan++) {
    *ogp++ = get_offset_register(chan);
    *ogp++ = get_gain_register(chan);
    *ogp++ = get_phase_register(chan);
  }
}

void set_ogp_registers(float *ogp) {
  int chan;

  for(chan = 1; chan < 5; chan++) {
    set_offset_register(chan, *ogp++);
    set_gain_register(chan, *ogp++);
    set_phase_register(chan, *ogp++);
  }
}

int read_adc_calibrations(void) {
  int i, status;
  static int adc_cal_valid = FALSE;

  if(!adc_cal_valid) {
    status = dsm_structure_init(&dsm_adc_cal,"SWARM_SAMPLER_CALIBRATIONS_X" );
    if (status != DSM_SUCCESS) {
      return FAIL;
    }
    if((i = dsm_read("obscon", "SWARM_SAMPLER_CALIBRATIONS_X",
        &dsm_adc_cal, NULL)) != DSM_SUCCESS){
      return FAIL;
    }
    adc_cal_valid = TRUE;
  }
  return(OK);
}

int write_adc_calibrations(void) {

  if(dsm_write("SWARM_MONITOR_AND_HAL", "SWARM_SAMPLER_CALIBRATIONS_X",
      &dsm_adc_cal) != DSM_SUCCESS){
    return FAIL;
  }
  return OK;
}

int dsm_write_actual_ogp(struct katcp_dispatch *d, int zdok, float *ogp) {
  float offs[2][4], gains[2][4], phases[2][4];
  int chan;

  if(read_adc_calibrations() != OK) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "Error reading adc calibrations structure from dsm");
    return(FAIL);
  }
  dsm_structure_get_element(&dsm_adc_cal, A_OFFS, offs);
  dsm_structure_get_element(&dsm_adc_cal, A_GAINS, gains);
  dsm_structure_get_element(&dsm_adc_cal, A_PHASES, phases);
  for(chan = 0; chan < 4; chan++) {
    offs[zdok][chan] = ogp[3 * chan];
    gains[zdok][chan] = ogp[3 * chan + 1];
    phases[zdok][chan] = ogp[3 * chan + 2];
  }
  dsm_structure_set_element(&dsm_adc_cal, A_OFFS, offs);
  dsm_structure_set_element(&dsm_adc_cal, A_GAINS, gains);
  dsm_structure_set_element(&dsm_adc_cal, A_PHASES, phases);
  if(write_adc_calibrations() != OK) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "Error writing adc calibrations structure to dsm");
    return(FAIL);
  }
  return OK;
}

int dsm_write_measured_ogp(struct katcp_dispatch *d, int zdok, og_rtn sum_og) {
  float offs[2][4], gains[2][4], oflow[2][4];
  float avz[2], avamp[2];
  int chan;

  if(read_adc_calibrations() != OK) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "Error reading adc calibrations structure from dsm");
    return(FAIL);
  }
  dsm_structure_get_element(&dsm_adc_cal, DEL_OFFS, offs);
  dsm_structure_get_element(&dsm_adc_cal, DEL_GAINS, gains);
  dsm_structure_get_element(&dsm_adc_cal, DEL_OVL_CNT , oflow);
  dsm_structure_get_element(&dsm_adc_cal, DEL_AVZ, avz);
  dsm_structure_get_element(&dsm_adc_cal, DEL_AVAMP, avamp);
  for(chan = 0; chan < 4; chan++) {
    offs[zdok][chan] = sum_og.ogp[3 * chan];
    gains[zdok][chan] = sum_og.ogp[3 * chan + 1];
    oflow[zdok][chan] = sum_og.overload_cnt[chan];
  }
  avz[zdok] = sum_og.avz;
  avamp[zdok] = sum_og.avamp;

  dsm_structure_set_element(&dsm_adc_cal, DEL_OFFS, offs);
  dsm_structure_set_element(&dsm_adc_cal, DEL_GAINS, gains);
  /* phases can not be measured for either zdok, so just set them to zero */
  memset((char *)&gains, 0, sizeof(gains));
  dsm_structure_set_element(&dsm_adc_cal, DEL_PHASES, gains);
  dsm_structure_set_element(&dsm_adc_cal, DEL_OVL_CNT , oflow);
  dsm_structure_set_element(&dsm_adc_cal, DEL_AVZ, avz);
  dsm_structure_set_element(&dsm_adc_cal, DEL_AVAMP, avamp);
  if(write_adc_calibrations() != OK) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "Error writing adc calibrations structure to dsm");
    return(FAIL);
  }
  return OK;
}

int check_ogp_registers(float *desired_ogp) {
  float read_back_ogp[12];
  int core, diff, errCnt;

  errCnt = 0;
  get_ogp_registers(read_back_ogp);
  for(core = 0; core < 4; core++) {
    diff = (int)(fabs(read_back_ogp[core*3] - desired_ogp[core*3])
       *255./100.1 + 0.5);
    errCnt += diff;
    diff = (int)(fabs(read_back_ogp[core*3 + 1] - desired_ogp[core*3 + 1])
       *255./36.03 + 0.5);
    errCnt += diff;
    diff = (int)(fabs(read_back_ogp[core*3 + 2] - desired_ogp[core*3 + 2])
       *255./28.03 + 0.5);
    errCnt += diff;
  }
  return(errCnt);
}

int read_ogp_file(struct katcp_dispatch *d, char *fname, float *ogp) {
  FILE *fp;
  int i;

  fp = fopen(fname, "r");
  if(fp == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
      "Error %s opening %s", strerror(errno), fname);
    return 0;
  }
  for(i = 0; i < 12; i++) {
    fscanf(fp, "%f", &ogp[i]);
  }
  fclose(fp);
  return 1;
}

int write_ogp_file(struct katcp_dispatch *d, char *fname, float *ogp) {
  FILE *fp;
  int i;

  umask(0000);
  fp = fopen(fname, "w");
  if(fp == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
      "Error %s opening %s", strerror(errno), fname);
    return 0;
  }
  for(i = 0; i < 12; i++) {
    fprintf(fp, "%f\n", ogp[i]);
  }
  fclose(fp);
  return 1;
}

void print_ogp(struct katcp_dispatch *d, float *ogp) {
  int chan;

  for(chan = 0; chan < 4; chan++) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
      "%.4f %.4f %.4f", ogp[3*chan], ogp[3*chan+1], ogp[3*chan+2]);
  }
}

int set_snapshot_pointers( struct snapshot_args args, int zdok) {
  struct tbs_entry *ctrl_reg, *data_reg, *status_reg;
#if SCOPE_SRC_CTRL
  struct tbs_entry *scope_ctrl_reg, *source_ctrl_reg;
#endif /* SCOPE_SRC_CTRL */
  static int cur_zdok = -99;

  if(zdok == cur_zdok) {
    return(1);
  }
  cur_zdok = zdok;
  /* Get the register pointers */
  if(zdok == 0) {
    ctrl_reg = find_data_avltree(args.tr->r_registers, SCOPE_0_CTRL);
    status_reg = find_data_avltree(args.tr->r_registers, SCOPE_0_STATUS);
    data_reg = find_data_avltree(args.tr->r_registers, SCOPE_0_DATA);
  } else {
    ctrl_reg = find_data_avltree(args.tr->r_registers, SCOPE_1_CTRL);
    status_reg = find_data_avltree(args.tr->r_registers, SCOPE_1_STATUS);
    data_reg = find_data_avltree(args.tr->r_registers, SCOPE_1_DATA);
  }
#if SCOPE_SRC_CTRL
  scope_ctrl_reg = find_data_avltree(args.tr->r_registers, SCOPE_CTRL);
  source_ctrl_reg = find_data_avltree(args.tr->r_registers, SOURCE_CTRL);
#endif /* SCOPE_SRC_CTRL */
  if(ctrl_reg == NULL || status_reg == NULL || data_reg == NULL
#if SCOPE_SRC_CTRL
      || scope_ctrl_reg == NULL || source_ctrl_reg == NULL
#endif /* SCOPE_SRC_CTRL */
    ) {
    log_message_katcp(args.d, KATCP_LEVEL_ERROR, NULL,
      "Snapshot registers not set up correctly");
    log_message_katcp(args.d, KATCP_LEVEL_ERROR, NULL,
      "%x %x %x", ctrl_reg, status_reg, data_reg);
    return 0;
  }
  snap_p = ((signed char *)(args.tr->r_map + data_reg->e_pos_base));
  ctrl_p = ((int *)(args.tr->r_map + ctrl_reg->e_pos_base));
  stat_p = ((int *)(args.tr->r_map + status_reg->e_pos_base));
#if SCOPE_SRC_CTRL
  scope_ctrl_p = ((int *)(args.tr->r_map + scope_ctrl_reg->e_pos_base));
  source_ctrl_p = ((int *)(args.tr->r_map + source_ctrl_reg->e_pos_base));
#endif /* SCOPE_SRC_CTRL */
  return 1;
}

int take_snapshot(struct snapshot_args args, int zdok) {
  int cnt;

  if(set_snapshot_pointers(args, zdok) == 0) {
    return 0;
  }
#if SCOPE_SRC_CTRL
  /* set up for a snapshot of the adc data */
  *scope_ctrl_p = 1536;
  *source_ctrl_p = 18;
#endif /* SCOPE_SRC_CTRL */
  /* trigger a snapshot capture */
  *ctrl_p = 2;
  *ctrl_p = 3;
  for(cnt = 0; *stat_p & 0x80000000; cnt++) {
    if(cnt > 100) {
      log_message_katcp(args.d, KATCP_LEVEL_ERROR, NULL, "Snapshot timed out");
      return 0;
    }
  }
  return *stat_p;	/* return length of snapshot */
}

/* Cores are read out in the sequence A, C, B, D , but should be in the natural
 * order in the ogp array. */
int startpos[] = {0, 2, 1, 3};
og_rtn og_from_noise(int len, signed char *snap) {
  og_rtn rtn;
  float avg, amp;
  int sum, i, cnt, core, code, start_i;

  memset((char *)&rtn, 0, sizeof(rtn));
  for(core = 0; core < 4; core++) {
    start_i = startpos[core];
    cnt = 0;
    sum = 0;
    amp = 0;
    for(i=start_i; i < len; i+= 4) {
      cnt++;
      code = snap[i];
      sum += code;
      if(code == -128 || code == 127) {
        rtn.overload_cnt[core]++;
      }
    }
    avg = (float)sum / cnt;
    for(i=start_i; i < len; i+= 4) {
      amp += fabs((float)snap[i] - avg);
    }
    avg *= (-500.0/256);
    rtn.avz += avg;
    rtn.avamp += amp/cnt;
    rtn.ogp[core*3] = avg;
    rtn.ogp[core*3 + 1] = amp/cnt;
  }
  rtn.avz /= 4;
  rtn.avamp /= 4;
  for(core = 0; core < 4; core++) {
    i = core*3 + 1;
    rtn.ogp[i] = 100 * (rtn.avamp - rtn.ogp[i])/rtn.avamp;
  }
  return(rtn);
}

void *monitor_adc(void *args) {
  int status, len;
  int hist_cnt, n, val, sum, ssq;
  int zdok;
  int hist[256][2];
  double avg;
  float loading_factor[2];
#define NHIST 60

  bzero(&hist, sizeof(hist));
  hist_cnt = 0;
  while(run_adc_monitor == TRUE) {
    for(zdok = 0; zdok < 2; zdok++) {
      sum = 0;
      ssq = 0;
      pthread_mutex_lock(&snap_mutex);
      len = take_snapshot(*(struct snapshot_args *)args, zdok);
      for(n = 0; n < len; n++) {
        val = snap_p[n];
        sum += val;
        ssq += val*val;
        hist[val+128][zdok]++;
      }
      pthread_mutex_unlock(&snap_mutex);
      avg = (double)sum/len;
      loading_factor[zdok] = -20*log10f((float)(128/sqrt((double)ssq/len -
            avg*avg)));
    }
    status = dsm_write("SWARM_MONITOR", ADC_LOADING, loading_factor);
    if(++hist_cnt >= NHIST) {
      status |= dsm_write("OBS", ADC_HIST, hist);
      hist_cnt = 0;
      bzero(&hist, sizeof(hist));
    }
    if (status != DSM_SUCCESS) {
      dsm_error_message(status, "writing adc loading factor or histogram");
    }
    sleep(1);
  }
  return args;
}

int get_snapshot_cmd(struct katcp_dispatch *d, int argc){
  int i = 0;
  int zdok = 0;
  int len;
  struct snapshot_args args;
  FILE *fp;

  /* Grab the mode pointer */
  if((args.tr = get_mode_pointer(d)) == NULL)
    return(KATCP_RESULT_FAIL);
  args.d = d;

  /* Grab the first argument, optional zdok will be zero if missing */
  zdok = arg_signed_long_katcp(d, 1);
  if (zdok < 0 || zdok > 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Bad zdok, must be 0 or 1");
    return KATCP_RESULT_FAIL;
  }

  umask(0000);
  if((fp = fopen(SNAP_NAME, "w")) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
      "Error %s opening %s", strerror(errno), SNAP_NAME);
    return KATCP_RESULT_FAIL;
  }
  pthread_mutex_lock(&snap_mutex);
  len = take_snapshot(args, zdok);
  if(len == 0) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Taking snapshot failed");
    fclose(fp);
    return KATCP_RESULT_FAIL;
  }
  for(i = 0; i < len; i++) {
    fprintf(fp, "%d\n", snap_p[i]);
  }
  pthread_mutex_unlock(&snap_mutex);
  fclose(fp);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
    "1st snapshot values = %d %d %d %d, status %d",
    (int)*snap_p, (int)snap_p[1], (int)snap_p[2], (int)snap_p[3], len);
  return KATCP_RESULT_OK;
}

int measure_og_cmd(struct katcp_dispatch *d, int argc){
  int rpt = 100;
  int zdok = 0;
  struct snapshot_args args;
  int i = 0, n;
  int len;
  og_rtn single_og, sum_og;
  char fname[36];

  /* Grab the mode pointer */
  if((args.tr = get_mode_pointer(d)) == NULL)
    return(KATCP_RESULT_FAIL);
  args.d = d;

  /* Grab the first argument, optional zdok will be zero if missing */
  zdok = arg_signed_long_katcp(d, 1);
  if (zdok < 0 || zdok > 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Bad zdok, must be 0 or 1");
    return KATCP_RESULT_FAIL;
  }
  if(argc > 2) {
    rpt = arg_signed_long_katcp(d, 2);
    if(rpt <= 0 || rpt > 2000) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "Bad repeat, must be in [1,2000]");
      return KATCP_RESULT_FAIL;
    }
  }
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "argc = %d,zdok=%d,rpt=%d", argc,zdok,rpt);
  memset((char *)&sum_og, 0, sizeof(sum_og));
#if 0
  if(set_snapshot_pointers(d, tr, zdok) == 0) {
    return KATCP_RESULT_FAIL;
  }
#endif
  for(n = rpt; n > 0; n--) {
    pthread_mutex_lock(&snap_mutex);
    len = take_snapshot(args, zdok);
    if(len == 0) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Taking snapshot failed");
      return KATCP_RESULT_FAIL;
    }
    single_og = og_from_noise(len, snap_p);
    pthread_mutex_unlock(&snap_mutex);
    sum_og.avz += single_og.avz;
    sum_og.avamp += single_og.avamp;
    for(i = 0; i < 12; i++) {
      sum_og.ogp[i] += single_og.ogp[i];
    }
    for(i = 0; i < 4; i++) {
      sum_og.overload_cnt[i] += single_og.overload_cnt[i];
    }
  }
#if 0
  umask(0000);
  if((fp = fopen(OGP_MEAS_NAME, "w")) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
      "Error %s opening %s", strerror(errno), OGP_MEAS_NAME);
    return KATCP_RESULT_FAIL;
  }
  fprintf(fp, "%8.4f\n%8.4f\n  0.000\n", sum_og.ogp[0]/rpt,sum_og.ogp[1]/rpt);
  fprintf(fp, "%8.4f\n%8.4f\n  0.000\n", sum_og.ogp[3]/rpt,sum_og.ogp[4]/rpt);
  fprintf(fp, "%8.4f\n%8.4f\n  0.000\n", sum_og.ogp[6]/rpt,sum_og.ogp[7]/rpt);
  fprintf(fp, "%8.4f\n%8.4f\n  0.000\n", sum_og.ogp[9]/rpt,sum_og.ogp[10]/rpt);
  fclose(fp);
#endif
  for(i = 0; i < 12; i++) {
    sum_og.ogp[i] /= rpt;
  }
  sprintf(fname, "%s%d%s", OGP_BASE_NAME, zdok, ".meas");
  if(write_ogp_file(d, fname, sum_og.ogp) == 0) {
    return KATCP_RESULT_FAIL;
  }
  dsm_write_measured_ogp(d, zdok, sum_og);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
    "avZero=%.4f,avAmp=%.4f", sum_og.avz/rpt, sum_og.avamp/rpt);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
    "%.4f,%.4f,%.4f", sum_og.ogp[0],sum_og.ogp[1],
    (float)sum_og.overload_cnt[0]/rpt);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
    "%.4f,%.4f,%.4f", sum_og.ogp[3],sum_og.ogp[4],
    (float)sum_og.overload_cnt[1]/rpt);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
    "%.4f,%.4f,%.4f", sum_og.ogp[6],sum_og.ogp[7],
    (float)sum_og.overload_cnt[2]/rpt);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL,
    "%.4f,%.4f,%.4f", sum_og.ogp[9],sum_og.ogp[10],
    (float)sum_og.overload_cnt[3]/rpt);
  return KATCP_RESULT_OK;
}

/* Optional arguments zdok [0] and fname [/instance/ogp] */
int set_ogp_cmd(struct katcp_dispatch *d, int argc){
  int zdok = 0;
  struct tbs_raw *tr;
  float ogp[12];
  char fname[36];
  int errCnt;

  /* Grab the mode pointer */
  if((tr = get_mode_pointer(d)) == NULL)
    return(KATCP_RESULT_FAIL);

  /* Grab the first argument, optional zdok will be zero if missing */
  zdok = arg_signed_long_katcp(d, 1);
  if (zdok < 0 || zdok > 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Bad zdok, must be 0 or 1");
    return KATCP_RESULT_FAIL;
  }
  if(argc > 2) {
    strcpy(fname, arg_string_katcp(d, 2));
  } else {
    sprintf(fname, "%s%d", OGP_BASE_NAME, zdok);
  }
  if(set_spi_pointer(d, tr, zdok) == 0) {
    return KATCP_RESULT_FAIL;
  }
  if(read_ogp_file(d, fname, ogp) == 0) {
    return KATCP_RESULT_FAIL;
  }
  set_ogp_registers(ogp);
  dsm_write_actual_ogp(d, zdok, ogp);
#if CHECK_READBACK 
  errCnt = check_ogp_registers(ogp);
  if(errCnt != 0) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "Sum of abs integer differences was %d setting ogp registers", errCnt);
    return KATCP_RESULT_FAIL;
  }
#endif
  return KATCP_RESULT_OK;
}

/* Optional arguments zdok [0] */
int get_ogp_cmd(struct katcp_dispatch *d, int argc){
  int zdok = 0;
  struct tbs_raw *tr;
  float ogp[12];

  /* Grab the mode pointer */
  if((tr = get_mode_pointer(d)) == NULL)
    return(KATCP_RESULT_FAIL);

  /* Grab the first argument, optional zdok will be zero if missing */
  zdok = arg_signed_long_katcp(d, 1);
  if (zdok < 0 || zdok > 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Bad zdok, must be 0 or 1");
    return KATCP_RESULT_FAIL;
  }
  if(set_spi_pointer(d, tr, zdok) == 0) {
    return KATCP_RESULT_FAIL;
  }
  get_ogp_registers(ogp);
  print_ogp(d, ogp);
  return KATCP_RESULT_OK;
}

/* Optional arguments zdok [0] */
int update_ogp_cmd(struct katcp_dispatch *d, int argc){
  int zdok = 0;
  int i;
  struct tbs_raw *tr;
  float ogp_reg[12], ogp_meas[12];
  char fname[36];

  /* Grab the mode pointer */
  if((tr = get_mode_pointer(d)) == NULL)
    return(KATCP_RESULT_FAIL);

  /* Grab the first argument, optional zdok will be zero if missing */
  zdok = arg_signed_long_katcp(d, 1);
  if (zdok < 0 || zdok > 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Bad zdok, must be 0 or 1");
    return KATCP_RESULT_FAIL;
  }
  if(set_spi_pointer(d, tr, zdok) == 0) {
    return KATCP_RESULT_FAIL;
  }
  sprintf(fname, "%s%d%s", OGP_BASE_NAME, zdok, ".meas");
  if(read_ogp_file(d, fname, ogp_meas) == 0) {
    return KATCP_RESULT_FAIL;
  }
#if UPDATE_FROM_OGP_FILE 
  /* assume that the ogp registers have been set from the ogp_if* file to
   * avoid readback errors */
  sprintf(fname, "%s%d", OGP_BASE_NAME, zdok);
  if(read_ogp_file(d, fname, ogp_reg) == 0) {
    return KATCP_RESULT_FAIL;
  }
  /* for each core round offset and gain to an offset of gain integer value */
  for(i = 0; i < 12; i+=3) {
    ogp_reg[i] = (100./255.)*round((255./100.)*ogp_reg[i]);
    ogp_reg[i] += ogp_meas[i];
    ogp_reg[i+1] = (36./255.)*round((255./36.)*ogp_reg[i+1]);
    ogp_reg[i+1] += ogp_meas[i];
    /* set phase to 0 */
    ogp_reg[i+2] = 0;
  }
#else
  get_ogp_registers(ogp_reg);
  for(i = 0; i < 12; i++) {
    ogp_reg[i] += ogp_meas[i];
  }
#endif
  sprintf(fname, "%s%d", OGP_BASE_NAME, zdok);
  if(write_ogp_file(d, fname, ogp_reg) == 0) {
    return KATCP_RESULT_FAIL;
  }
  set_ogp_registers(ogp_reg);
  dsm_write_actual_ogp(d, zdok, ogp_reg);
#if CHECK_READBACK  
  i = check_ogp_registers(ogp_reg);
  if(i != 0) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
        "Sum of abs integer differences was %d setting ogp registers", i);
    return KATCP_RESULT_FAIL;
  }
#endif
  return KATCP_RESULT_OK;
}

int start_adc_monitor_cmd(struct katcp_dispatch *d, int argc){
  int status;
  static struct snapshot_args args;

  /* Grab the mode pointer */
  if((args.tr = get_mode_pointer(d)) == NULL)
    return(KATCP_RESULT_FAIL);
  args.d = d;

  /* Set flag run adc monitoring of input level and histogram */
  run_adc_monitor = TRUE;
  /* Start the thread */
  status = pthread_create(&adc_monitor_thread,NULL, monitor_adc, (void *)&args);
  if (status < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL,
                "could not create adc_monitor thread");
    return KATCP_RESULT_FAIL;
  }
  return KATCP_RESULT_OK;
}

int stop_adc_monitor_cmd(struct katcp_dispatch *d, int argc){

  /* Set flag to stop adc_monitor measuring and reporting */
  run_adc_monitor = FALSE;

  /* Wait until the thread stops */
  pthread_join(adc_monitor_thread, NULL);

  return KATCP_RESULT_OK;
}

struct PLUGIN KATCP_PLUGIN = {
  .n_cmds = 7,
  .name = "sma_adc",
  .version = KATCP_PLUGIN_VERSION,
  .init = start_adc_monitor_cmd,
  .uninit = stop_adc_monitor_cmd,
  .cmd_array = {
    {
      .name = "?get-snapshot", 
      .desc = "get a snapshot and store it in /instance/snap.  Optional argument zdok",
      .cmd = get_snapshot_cmd
    },
    {
      .name = "?measure-og", 
      .desc = "Take repeated snapshots and mesasure offset and gain assuming noise input.  Write result in /instance/configFiles/ogp_if[01].meas.  Optional arguments zdoc (0) and rpt(30)",
      .cmd = measure_og_cmd
    },
    {
      .name = "?get-ogp", 
      .desc = "read ogp from the adc and print",
      .cmd = get_ogp_cmd
    },
    {
      .name = "?set-ogp", 
      .desc = "read ogp from a file and set the adc ogp registers.  Optional arguments zdok [0] and fname [/instance/configFiles/ogp_if[01]]",
      .cmd = set_ogp_cmd
    },
    {
      .name = "?update-ogp", 
      .desc = "read /instance/configFiles/ogp_if[01].meas and add to the ogp registers in the adc.  Also store results in /instance/configFiles/ogp_if[01].  Optional argument zdok",
      .cmd = update_ogp_cmd
    },
    {
      .name = "?start-adc-monitor", 
      .desc = "start the adc monitor thread",
      .cmd = start_adc_monitor_cmd
    },
    {
      .name = "?stop-adc-monitor", 
      .desc = "stop the adc monitor thread",
      .cmd = stop_adc_monitor_cmd
    },
  }
};
